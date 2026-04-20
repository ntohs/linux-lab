# linux-lab

## 요약

프로그래머스 데브코스 **리눅스 시스템 및 커널 전문가 과정 2기**에서 라즈베리파이 4를 타겟 보드로,

IPC, 디바이스 드라이버, 멀티스레드, GPIO 인터럽트 등을 활용한 수업과 토이 프로젝트를 기반으로 합니다.

현재 업무 도메인인 무선랜 스택을 추가하고 리팩토링 하여 임베디드 리눅스 Wi-Fi AP 모니터링 프로젝트를 구현했습니다.

## 📁 폴더 구조

| 디렉터리 | 설명 |
|----------|------|
| `src/` | 메인 애플리케이션 (`linux_lab`) — 프로세스 관리, IPC, CLI, 시스템 상태 덤프 |
| `wlinkd/` | Wi-Fi AP 모니터링 데몬 — libnl/nl80211 기반, hostapd를 참고해 제작 |
| `drivers/` | 커널 모듈 — GPIO LED 드라이버, 버튼 인터럽트 드라이버 |
| `hostap/` | hostapd 소스 (WPS-PBC 등 AP 제어 참조용) |

## 빌드 및 실행

```bash
# 전체 빌드 (src + wlinkd + drivers)
make

# AP 모드 구성 (최초 1회)
sudo ./wlinkd/setup-pidev-ap.sh

# 드라이버 적재
sudo ./install_drivers.sh

# 실행 (-d: 디버그, -dd: 상세 디버그)
sudo ./linux_lab
```

## 동작 설명

### LED 시나리오 (GPIO LED 드라이버)

`/dev/toy_simple_io_driver`에 명령 문자열을 write하여 LED를 제어합니다.

- **기본 상태**: LED OFF
- **1대 이상의 Wi-Fi 단말 접속 시**: LED ON (`sta_join`)
- **접속된 단말에서 트래픽 발생 시**: LED Blink (`traffic`)
- **모든 단말이 접속 해제되면**: LED OFF (`sta_leave`)

### 버튼 (GPIO 인터럽트 드라이버)

`/dev/kdt_interrupt_driver`를 통해 버튼 인터럽트를 유저스페이스로 전달합니다.

- 버튼 누름 → 커널 IRQ → `SIGUSR1` 시그널 → system_server의 button_watcher 스레드
- `hostapd_cli wps_pbc` 실행으로 WPS 간편 접속 수행합니다.
- 보안상 권장하지 않으나, 버튼 인터럽트, 시그널 전달 시연을 위해 구현했습니다.

## 프로세스 / 스레드 구조

### Main 프로세스 (`linux_lab`)

- CLI 옵션 (`-h`, `-d`, `-f <logfile>`)
- POSIX 메시지 큐 3개 생성 (`/watchdog_queue`, `/monitor_queue`, `/disk_queue`)
- 자식 프로세스 3개 `fork`

### input 프로세스

`command_thread`에서 대화형 CLI 셸을 실행합니다.

| 명령 | 기능 |
|------|------|
| `wlan` | wlinkd에 `WLAN_DUMP` 요청 → AP/STA 상태 출력 |
| `dump` | `/monitor_queue`로 `DUMP_STATE` 전송 → 시스템 상태 덤프 |
| `sh` | `fork` + `execvp`로 임의의 셸 명령 실행 |
| `elf` | `/proc/self/exe`를 `mmap`하여 ELF 헤더 분석 |
| `mincore` | `mmap` + `mincore()`로 페이지 상주 여부 확인 |
| `mu` | mutex 잠금 아래 전역 문자열 저장 |
| `busy` | CPU 부하 테스트 (무한 루프) |
| `help` | 전체 명령어 목록 |
| `exit` | 종료 |

### system_server 프로세스

7개의 스레드를 생성하여 시스템 서비스를 운용합니다.

| 스레드 | 역할 |
|--------|------|
| `watchdog_thread` | `/watchdog_queue` 대기, 수신된 메시지 로깅 |
| `monitor_thread` | `/monitor_queue` 대기, `DUMP_STATE` 수신 시 `dumpstate()` 실행 |
| `disk_service_thread` | `inotify`로 `./fs` 디렉터리 모니터링, 파일 생성 감지 및 디렉터리 크기 산출 |
| `timer_thread` | `setitimer(ITIMER_REAL)` + `SIGALRM` → POSIX 세마포어로 1초 주기 타이머 |
| `engine_thread` | `SCHED_RR` 우선순위 50, CPU 코어 0 고정 (RT 스케줄링 시연) |
| `led_watcher_thread` | wlinkd Unix 소켓에 `SUBSCRIBE` → `"sta_join"`/`"sta_leave"`/`"traffic"` 이벤트 수신 → LED 드라이버 제어 |
| `button_watcher_thread` | 커널 드라이버의 `SIGUSR1` 수신 → `hostapd_cli wps_pbc` 실행 |

### wlinkd 데몬

`launcher.c`에서 `fork` + `execl`로 실행됩니다. 상세 내용: [wlinkd/README.md](wlinkd/README.md)

## 활용된 리눅스 기술 요소

| 분류 | 기술 |
|------|------|
| **IPC** | 메시지 큐, 공유 메모리, AF_UNIX 소켓, 시그널 (`SIGUSR1`, `SIGCHLD`, `SIGALRM`) |
| **스레드** | pthread, mutex, condition variable, POSIX 세마포어, `SCHED_RR`, CPU affinity |
| **드라이버** | char device (`alloc_chrdev_region`), GPIO (`gpio_request`/`gpio_direction`), IRQ (`request_irq`), wait queue, kernel thread |
| **네트워크** | nl80211, libnl, Netlink 멀티캐스트 이벤트, `poll` 기반 이벤트 루프 |
| **시스템** | `fork`/`exec`, `prctl`, `inotify`, `mmap`, `mincore`, `klogctl`, `/proc` 파싱, `timerfd` |

## 라이선스

BSD License — Copyright (c) 2026, Jaewon Park

wlinkd 서브프로젝트는 hostapd (Copyright (c) 2002-2022, Jouni Malinen)의 코드를 일부 참조했습니다.
