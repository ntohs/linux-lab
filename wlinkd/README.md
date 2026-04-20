# wlinkd

Wi-Fi AP 상태 모니터링 데몬 — 라즈베리파이 4에서 libnl/nl80211를 활용해 연결된 단말(STA) 정보를 실시간으로 수집하고,

Unix 소켓 IPC로 상위 애플리케이션에 전달합니다.


실무에서 사용하던 오픈소스 **hostapd**의 nl80211 활용 구조를 참고해 제작했으며,

칩 벤더 전용 SDK 대신 리눅스 표준 인터페이스만으로 구현하는 것을 목표로 했습니다.

```
유저(wlinkd) <-> libnl <-> 커널(netlink - brcmfmac)
```

## 빌드

```bash
# 의존 패키지 설치 (Debian/Ubuntu)
sudo apt install libnl-3-dev libnl-genl-3-dev

# 빌드
make

# 실행 (-s: IPC 소켓 활성화, -d: 디버그 로그, -dd: 상세 디버그)
sudo ./wlinkd -s -dd -i wlan0
```

## 아키텍처

단일 스레드, `poll` 기반 이벤트 루프로 동작합니다. 계층을 분리하여 각 레이어가 독립적으로 교체 가능하도록 설계했습니다.

```
┌──────────────────────────────────────────────┐
│  App Layer                                   │
│  main.c — option, poll loop, signal handle   │
├──────────────────────────────────────────────┤
│  Domain/Service Layer (Facade)               │
│  wlan_core.c — init/refresh/build_status     │
├──────────────────┬───────────────────────────┤
│  STA Manager     │  IPC Server               │
│  wlan_sta_mgr.c  │  ipc.c                    │
│  linked list     │  AF_UNIX socket           │
├──────────────────┴───────────────────────────┤
│  Driver/Protocol Layer                       │
│  wlan_nl80211.c — GET_INTERFACE,             │
│  GET_STATION, MLME event get & parse         │
├──────────────────────────────────────────────┤
│  Netlink API Layer                           │
│  wlan_libnl.c — send/recv, callback          │
│  error handle by libnl (nl80211 api)         │
├──────────────────────────────────────────────┤
│  Kernel Interface                            │
│  nl80211 + Netlink                           │
└──────────────────────────────────────────────┘
```

### 계층 설계

- **nl80211**: 커널이 제공하는 무선 제어 인터페이스 (Generic Netlink family)
- **libnl**: 유저스페이스에서 Netlink/nl80211을 다루기 위한 라이브러리

`main.c`가 `wlan_core.c`만 참고하도록 하여 libnl에 직접 의존하지 못하도록 했습니다.

`wlan_core.c`를 마치 HAL처럼 동작하도록 두어 libnl이 아닌 타사 칩의 api 확장을 고려했습니다.

또한 내부 모듈(`wlan_libnl`, `wlan_nl80211`, `wlan_sta_mgr`)을 캡슐화됩니다.

### Netlink 소켓 분리

libnl 소켓을 2개로 분리하여 이벤트와 요청/응답이 섞이는 문제를 방지합니다.

| 소켓 | 용도 |
|------|------|
| `g_event_sock` | MLME 멀티캐스트 이벤트 수신 전용 (`poll` 대상) |
| `g_req_sock` | `GET_INTERFACE`/`GET_STATION` 요청·응답 전용 |

## 이벤트 루프

`poll`로 3개의 fd를 감시합니다.

| fd | 소스 | 처리 |
|----|------|------|
| IPC fd | AF_UNIX `/tmp/wlinkd.sock` | 클라이언트 명령 처리 (`WLAN_PING`, `WLAN_DUMP`, `SUBSCRIBE`) |
| nl80211 event fd | Netlink mlme 그룹 | `NEW_STATION`/`DEL_STATION` 이벤트 → STA 목록 갱신 |
| timerfd (1초) | 주기 타이머 | `wlan_core_refresh()` → AP 정보 + STA dump 갱신 |

## STA 추적 흐름

STA(단말)는 두 가지 경로로 추적됩니다.

### 1. 실시간 MLME 이벤트

```
커널 mlme 이벤트 → g_event_sock → poll wakeup
  → NL80211_CMD_NEW_STATION  → wlan_sta_mgr_add()  + ipc_push_sta_join()
  → NL80211_CMD_DEL_STATION  → wlan_sta_mgr_remove() + ipc_push_sta_leave()
```

### 2. 주기적 Station Dump (1초)

```
timerfd → wlan_nl80211_refresh()
  → NL80211_CMD_GET_INTERFACE  → AP 모드/채널/대역폭 갱신
  → NL80211_CMD_GET_STATION    → STA별 RSSI/비트레이트/패킷 수 갱신
  → 패킷 증가 감지 시          → ipc_push_traffic()
  → dump에 없는 STA 제거       → ipc_push_sta_leave()
```

## IPC 프로토콜

AF_UNIX 스트림 소켓 (`/tmp/wlinkd.sock`)으로 통신합니다.

### 요청-응답 명령

| 명령 | 응답 |
|------|------|
| `WLAN_PING` | `PONG AP` 또는 `PONG MONITOR` |
| `WLAN_DUMP` | AP 기본 정보 + 연결된 STA 목록 (텍스트) |

### Push 이벤트 (`SUBSCRIBE` 등록 후)

| 이벤트 | 발생 조건 |
|--------|-----------|
| `sta_join` | 1대 이상의 단말이 접속 |
| `sta_leave` | 마지막 단말이 접속 해제 |
| `traffic` | 접속 중인 단말의 패킷 수 증가 감지 |

linux-lab의 `led_watcher_thread`가 `SUBSCRIBE`로 등록한 뒤 이 이벤트를 수신해 LED 드라이버를 제어합니다.

## 파일 구조

| 파일 | 역할 |
|------|------|
| `src/main.c` | poll 이벤트 루프, 시그널 처리, LED 초기화 |
| `src/wlan_core.c` | 무선 API |
| `src/wlan_nl80211.c` | nl80211 명령 전송 및 응답/이벤트 콜백 처리 |
| `src/wlan_libnl.c` | libnl 소켓 관리, send/recv 공통 처리 |
| `src/wlan_sta_mgr.c` | STA 연결리스트 (추가/제거/조회/카운트) |
| `src/ipc.c` | AF_UNIX IPC 서버, push 이벤트 발행 |
| `src/wlan_utils.c` | BSSID 포맷, 채널 변환, IP 조회, STA 검색 |
| `common/debug.c` | 타임스탬프 로깅 (ERROR/INFO/DEBUG 레벨) |
| `setup-pidev-ap.sh` | 라즈베리파이 4 AP 모드 자동 구성 (hostapd, dnsmasq, NAT) |

## 라이선스

BSD License — Copyright (c) 2026, Jaewon Park

hostapd (Copyright (c) 2002-2022, Jouni Malinen)의 nl80211 구조를 참조했습니다.
