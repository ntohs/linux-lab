## 요약

프로그래머스 데브코스 **리눅스 시스템 및 커널 전문가 과정 2기**에서 라즈베리파이 4를 타겟 보드로,

IPC, 디바이스 드라이버, 멀티스레드, GPIO 인터럽트 등을 활용한 수업과 토이 프로젝트를 기반으로 합니다.

현재 업무 도메인인 무선랜 스택을 추가하고 리팩토링 하여 임베디드 리눅스 Wi-Fi AP 모니터링 프로젝트를 구현했습니다.



## 폴더 구조

| 디렉터리 | 설명 |
|----------|------|
| `src/` | 메인 애플리케이션 (`linux_lab`) - 프로세스 관리, IPC, CLI, 시스템 상태 덤프 |
| `wlinkd/` | Wi-Fi AP 모니터링 데몬 - libnl/nl80211 기반, TCP 서버 포트(9000) 내장 |
| `drivers/` | 커널 모듈 - GPIO LED, 버튼 인터럽트, BMP280 SPI 온도 센서(DMA/PIO 비교) |



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



## 동작


### BMP280 SPI 온도 센서 드라이버 (`sensor_spi.ko`)

강의에서 제공된 BMP280 I2C 드라이버 레퍼런스 코드를 SPI로 포팅하고, BCM2711의 DMA/PIO 동작 학습 목적 모듈입니다.

| 항목 | 내용 |
|------|------|
| **sysfs 제어** | `echo <0, 1> \| sudo tee /sys/sensor_spi/use_dma`로 DMA/PIO 전환 |
| **sysfs 알림** | `poll()` 로 `notify` 속성을 감시해 1초 주기 온도 변화 수신 |
| **타이머** | 드라이버 단에서 `hrtimer` + `workqueue` 조합으로 주기적 센서 읽기 |
| **DMA 방식** | 512바이트 단일 전송 → BCM2835 드라이버가 자동으로 DMA 선택(≥96바이트) |
| **PIO 방식** | 95바이트씩 분할 전송 → BCM2835 드라이버가 PIO 강제 |

**측정 결과** (10MHz SPI, 512바이트, Raspberry Pi 4):

```
PIO (dma_mode=0): ~650 μs
DMA (dma_mode=1): ~480 μs
```


### Wi-Fi 관련 동작

#### [wlinkd](wlinkd/README.md) 프로세스 TCP 서버 (9000 포트) (`./wlinkd/wlinkd`)

`./linux-lab` 프로세스 실행 시 `./wlinkd/wlinkd` 프로세스도 같이 실행되도록 구현했습니다.

wlinkd 데몬 내부에서 별도 스레드로 동작해, 외부 TCP 클라이언트에 WLAN 이벤트를 실시간으로 제공합니다.

| 클라이언트 요청 | 서버 응답 |
|------|------|
| **status** | 현재 AP/STA 상태를 응답 |
| **monitor** | STA 접속 이벤트를 응답 |
| **exit** | 클라이언트 연결을 종료 |

#### LED 시나리오 (GPIO LED 드라이버)

`/dev/toy_simple_io_driver`에 명령 문자열을 write하여 LED를 제어합니다.

- **기본 상태**: LED OFF
- **1대 이상의 Wi-Fi 단말 접속 시**: LED ON (`sta_join`)
- **접속된 단말에서 트래픽 발생 시**: LED Blink (`traffic`)
- **모든 단말이 접속 해제되면**: LED OFF (`sta_leave`)

#### 버튼 (GPIO 인터럽트 드라이버)

`/dev/kdt_interrupt_driver`를 통해 버튼 인터럽트를 유저스페이스로 전달합니다.

- 버튼 누름 → 커널 IRQ → `SIGUSR1` 시그널 → system_server의 `button_watcher_thread`
- `hostapd_cli wps_pbc` 실행으로 WPS 간편 접속 수행합니다.
- 보안상 권장하지 않으나, 버튼 인터럽트와 시그널 전달 흐름 시연을 위해 구현했습니다.


### 프로세스 / 스레드 구조

#### Main 프로세스 (`linux_lab`)

- CLI 옵션 (`-h`, `-d`, `-f <logfile>`)
- POSIX 메시지 큐 3개 생성 (`/watchdog_queue`, `/monitor_queue`, `/disk_queue`)
- 자식 프로세스 2개 `fork` (`system_server`, `input`)
- wlinkd 데몬 1개 `fork` + `execl` (별개 바이너리 실행)

#### input 프로세스

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

#### system_server 프로세스

7개의 스레드를 생성하여 시스템 서비스를 운용합니다.

| 스레드 | 역할 |
|--------|------|
| `watchdog_thread` | `/watchdog_queue` 대기, 수신된 메시지 로깅 |
| `monitor_thread` | `/monitor_queue` 대기, `DUMP_STATE` 수신 시 `dumpstate()` 실행 |
| `disk_service_thread` | `inotify`로 `./fs` 디렉터리 모니터링, 파일 생성 감지 및 디렉터리 크기 산출 |
| `timer_thread` | `setitimer(ITIMER_REAL)` + `SIGALRM` → POSIX 세마포어로 1초 주기 타이머 |
| `engine_thread` | `SCHED_RR` 우선순위 50, CPU 코어 0 고정 (RT 스케줄링 시연) |
| `led_watcher_thread` | wlinkd Unix 소켓 `SUBSCRIBE` → STA 이벤트 수신 → LED 드라이버 제어 |
| `button_watcher_thread` | 커널 드라이버의 `SIGUSR1` 수신 → `hostapd_cli wps_pbc` 실행 |

#### 동기화 설계

| 동기화 기법 | 사용 위치 | 선택 이유 |
|---|---|---|
| **POSIX 세마포어** (`sem_post` / `sem_wait`) | SIGALRM 핸들러 → `timer_thread` | signal context에서는 async-signal-safe 함수만 허용 - `pthread_mutex_lock` 사용 불가하여 `sem_post` 선택 |
| **mutex** (`timer_mutex`) | `system_timeout_handler` | 여러 스레드가 참조하는 전역 `timer` 변수의 원자성 보장 |
| **mutex** (`print_lock`) | 전체 스레드 로그 출력 | 여러 스레드가 동시에 `printf`할 때 출력 뒤섞임 방지 |
| **mutex** (`global_message_mutex`) | `input.c` - `mu` 명령 | CLI 스레드와 내부 스레드 간 공유 문자열 경합 방지 |
| **POSIX 메시지 큐** (3개) | 프로세스 전체 | 큐 삽입, 추출 자체가 원자적 - Producer/Consumer 분리, 별도 락 불필요 |

#### wlinkd 데몬 (`./wlinkd/wlinkd`)

`launcher.c`에서 `fork` + `execl`로 실행됩니다. 상세 내용: [wlinkd/README.md](wlinkd/README.md)


### 활용된 리눅스 기술 요소

| 분류 | 기술 |
|------|------|
| **IPC** | 메시지 큐, 공유 메모리, AF_UNIX 소켓, 시그널 (`SIGUSR1`, `SIGCHLD`, `SIGALRM`) |
| **스레드** | pthread, mutex, condition variable, POSIX 세마포어, `SCHED_RR`, CPU affinity |
| **드라이버** | char device (`alloc_chrdev_region`), GPIO (`gpio_request`/`gpio_direction`), IRQ (`request_irq`), wait queue, kernel thread |
| **SPI/DMA** | `spi_new_device`, `spi_sync`, BCM2835 자동 DMA(≥96B), `hrtimer`, `workqueue`, `sysfs_notify` (`kobject`/`kobj_type`) |
| **Wi-Fi** | libnl/nl80211 Netlink 멀티캐스트 이벤트 구독, `poll` 기반 이벤트 루프, TCP 관리 포트 (`pthread_cond_timedwait`, `MSG_DONTWAIT`) |
| **시스템** | `fork`/`exec`, `prctl`, `inotify`, `mmap`, `mincore`, `klogctl`, `/proc` 파싱, `timerfd` |


### 라이선스

BSD License - Copyright (c) 2026, Jaewon Park

wlinkd 서브프로젝트는 hostapd (Copyright (c) 2002-2022, Jouni Malinen)의 코드를 일부 참조했습니다.
