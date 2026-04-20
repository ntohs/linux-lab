/*
 * Based on KDT Linux Expert Course.
 * System server process
 * Copyright (c) 2026, Jaewon Park <pf0119@gmail.com>
 *
 * This software may be distributed under the terms of the BSD license.
 * See LICENSE for more details.
 */

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include "input.h"
#include "launcher.h"
#include "worker.h"

#include <assert.h>
#include <dirent.h>
#include <mqueue.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <signal.h>
#include <sys/inotify.h>
#include <sys/time.h>
#include <time.h>

#include "debug.h"
#include "dumpstate.h"
#include "message.h"
#include "shm.h"

extern int g_wps_requested;

#define BUFSIZE 1024

pthread_mutex_t system_loop_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t system_loop_cond = PTHREAD_COND_INITIALIZER;
bool system_loop_exit = false;

/* 6.3.2. POSIX 메시지 큐 */
static mqd_t watchdog_queue;
static mqd_t monitor_queue;
static mqd_t disk_queue;

/* 6.2.4. 타이머 */
static int timer = 0;

/* 6.4.1. POSIX 세마포어 & 공유메모리 */
pthread_mutex_t timer_mutex = PTHREAD_MUTEX_INITIALIZER;
static sem_t global_timer_sem;
static bool global_timer_stopped;

/* 스레드끼리 출력이 엉켜서 만듬 */
pthread_mutex_t print_lock = PTHREAD_MUTEX_INITIALIZER;

/* 6.4.2. 시스템 V 메시지 큐 & 세마포어 & 공유 메모리 */

static void timer_expire_signal_handler()
{
    /* 6.4.1. POSIX 세마포어 & 공유메모리 */
    // signal 문맥에서는 비동기 시그널 안전 함수(async-signal-safe function) 사용
    // man signal 확인
    // sem_post는 async-signal-safe function
    // 여기서는 sem_post 사용
    sem_post(&global_timer_sem);
}

static void system_timeout_handler()
{
    /* 6.4.1. POSIX 세마포어 & 공유메모리 */
    // 여기는 signal hander가 아니기 때문에 안전하게 mutex lock 사용 가능
    // timer 변수는 전역 변수이므로 뮤텍스를 사용한다
    pthread_mutex_lock(&timer_mutex);
    timer++;
    // printf("timer: %d\n", timer);
    pthread_mutex_unlock(&timer_mutex);
    return;
}

void set_periodic_timer(long sec_delay, long usec_delay)
{
    /*
    struct itimerval itimer_val = {
        .it_interval = { .tv_sec = sec_delay, .tv_usec = usec_delay },
        .it_value = { .tv_sec = sec_delay, .tv_usec = usec_delay }
    };
    */
    struct itimerval itimer_val;

    itimer_val.it_interval.tv_sec = sec_delay;
    itimer_val.it_interval.tv_usec = usec_delay;
    itimer_val.it_value.tv_sec = sec_delay;
    itimer_val.it_value.tv_usec = usec_delay;

    setitimer(ITIMER_REAL, &itimer_val, (struct itimerval *)0);

    return;
}

int posix_sleep_ms(unsigned int timeout_ms)
{
    struct timespec sleep_time;

    sleep_time.tv_sec = timeout_ms / MILLISEC_PER_SECOND;
    sleep_time.tv_nsec = (timeout_ms % MILLISEC_PER_SECOND) * (NANOSEC_PER_USEC * USEC_PER_MILLISEC);

    return nanosleep(&sleep_time, NULL);
}

static void *timer_thread(void *not_used)
{
    (void)not_used;
    struct sigaction sigact;
    int ret;

    if (g_debug_level == LOG_DEBUG) {
        pthread_mutex_lock(&print_lock);
        log_debug("timer thread started");
        pthread_mutex_unlock(&print_lock);
    }

    sigact.sa_flags = SA_SIGINFO;
    sigact.sa_sigaction = timer_expire_signal_handler;
    ret = sigaction(SIGALRM, &sigact, NULL);
    if (ret == -1) {
        log_error("sigaction err!!");
        assert(ret == 0);
    }

    set_periodic_timer(1, 1);

    /* 6.4.1. POSIX 세마포어 & 공유메모리 */
    while (!global_timer_stopped) {
        ret = sem_wait(&global_timer_sem);
        if (ret == -1) {
            if (errno == EINTR)
                continue;
            else
                assert(ret == 0);
        }

        sleep(1);
        system_timeout_handler();
    }

    return NULL;
}

/* 6.2.5. 스레드 */
void *watchdog_thread(void *arg)
{
    int ret;
    mq_msg_t msg;

    if (g_debug_level == LOG_DEBUG) {
        pthread_mutex_lock(&print_lock);
        log_debug("%s started", (char *)arg);
        pthread_mutex_unlock(&print_lock);
    }

    while (1) {
        /* 6.3.2. POSIX 메시지 큐 */
        ret = (int)mq_receive(watchdog_queue, (void *)&msg, sizeof(mq_msg_t), 0);
        assert(ret >= 0);
        log_debug("%s : 메시지가 도착했습니다.", __func__);
        log_debug("msg.type : %d", msg.msg_type);
        log_debug("msg.param1 : %d", msg.param1);
        log_debug("msg.param2 : %d", msg.param2);
    }

    return NULL;
}

#define SENSOR_DATA 1
#define DUMP_STATE 2

void *monitor_thread(void *arg)
{
    int ret;
    mq_msg_t msg;

    if (g_debug_level == LOG_DEBUG) {
        pthread_mutex_lock(&print_lock);
        log_debug("%s started", (char *)arg);
        pthread_mutex_unlock(&print_lock);
    }

    while (1) {
        /* 6.3.2. POSIX 메시지 큐 */
        ret = (int)mq_receive(monitor_queue, (void *)&msg, sizeof(mq_msg_t), 0);
        assert(ret >= 0);
        log_debug("%s : 메시지가 도착했습니다.", __func__);
        log_debug("msg.type : %d", msg.msg_type);
        log_debug("msg.param1 : %d", msg.param1);
        log_debug("msg.param2 : %d", msg.param2);
        /* 6.4.2. 시스템 V 메시지 큐 & 세마포어 & 공유 메모리 */
        if (msg.msg_type == DUMP_STATE) {
            dumpstate();
        } else {
            log_error("monitor_thread: unknown message. xxx");
        }
    }

    return NULL;
}

/* 6.4.4. 파일 시스템 관련 시스템 콜 */
// https://stackoverflow.com/questions/21618260/how-to-get-total-size-of-subdirectories-in-c
static long get_directory_size(char *dirname)
{
    DIR *dir = opendir(dirname);
    if (dir == 0)
        return 0;

    struct dirent *dit;
    struct stat st;
    long size = 0;
    long total_size = 0;
    char filePath[1024];

    while ((dit = readdir(dir)) != NULL) {
        if ((strcmp(dit->d_name, ".") == 0) || (strcmp(dit->d_name, "..") == 0))
            continue;

        sprintf(filePath, "%s/%s", dirname, dit->d_name);
        if (lstat(filePath, &st) != 0)
            continue;
        size = st.st_size;

        if (S_ISDIR(st.st_mode)) {
            long dir_size = get_directory_size(filePath) + size;
            total_size += dir_size;
        } else {
            total_size += size;
        }
    }
    return total_size;
}

void *disk_service_thread(void *arg)
{
    int inotifyFd, wd;
    char buf[BUFSIZE] __attribute__((aligned(8)));
    ssize_t numRead;
    char *p;
    struct inotify_event *event;
    char *directory = "./fs";
    int total_size;

    if (g_debug_level == LOG_DEBUG) {
        pthread_mutex_lock(&print_lock);
        log_debug("%s started", (char *)arg);
        pthread_mutex_unlock(&print_lock);
    }

    inotifyFd = inotify_init();
    if (inotifyFd == -1)
        return 0;

    wd = inotify_add_watch(inotifyFd, directory, IN_CREATE);
    if (wd == -1)
        return 0;

    while (1) {
        numRead = read(inotifyFd, buf, BUFSIZE);
        if (!numRead) {
            log_error("read() from inotify fd returned 0!");
            return 0;
        } else if (numRead == -1) {
            log_error("read() failed!!");
            return 0;
        }

        for (p = buf; p < buf + numRead;) {
            event = (struct inotify_event *)p;
            p += sizeof(struct inotify_event) + event->len;
        }
        total_size = get_directory_size(directory);
        log_debug("directory size: %d", total_size);
    }

    return 0;
}

/* 9.2.3. 리눅스 스케줄러 (FIFO, RR) 분석 및 활용 */
// 디바이스 드라이버를 real time으로 구동하기 위한 부분(?)
void *engine_thread()
{
    struct sched_param sched;
    cpu_set_t set;
    CPU_ZERO(&set);

    memset(&sched, 0, sizeof(sched));
    sched.sched_priority = 50;

    if (g_debug_level == LOG_DEBUG) {
        pthread_mutex_lock(&print_lock);
        log_debug("rr thread started [%d]", gettid());
        pthread_mutex_unlock(&print_lock);
    }

    if (sched_setscheduler(gettid(), SCHED_RR, &sched) < 0) {
        log_error("sched_setscheduler failed: %s", strerror(errno));
        if (errno == EPERM || errno == EACCES) {
            log_error("continuing without real-time priority");
        } else {
            exit(EXIT_FAILURE);
        }
    } else {
        log_debug("Priority set to \"%d\"", sched.sched_priority);
    }

    CPU_SET(0, &set);

    if (sched_setaffinity(gettid(), sizeof(set), &set) == -1) {
        log_error("sched_setaffinity failed: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // 임시 구동
    while (1)
        sleep(10000);

    return 0;
}

/* wlinkd IPC 구독 후 FSM 이벤트(sta_join/sta_leave/traffic) 수신 →
 * /dev/toy_simple_io_driver 에 전달됨 */
static void *led_watcher_thread()
{
    int sock_fd = -1;
    int led_fd;
    struct sockaddr_un addr;
    char buf[64];
    struct pollfd pfd;
    ssize_t n;

    if (g_debug_level == LOG_DEBUG) {
        pthread_mutex_lock(&print_lock);
        log_debug("led watcher thread started");
        pthread_mutex_unlock(&print_lock);
    }
    sleep(2); /* wlinkd 시작 대기 */

retry:
    if (sock_fd >= 0) {
        close(sock_fd);
        sock_fd = -1;
        sleep(2);
    }

    sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        log_error("socket: %s", strerror(errno));
        goto retry;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, "/tmp/wlinkd.sock", sizeof(addr.sun_path) - 1);

    if (connect(sock_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        log_debug("wlinkd 연결 실패, 재시도");
        goto retry;
    }
    if (send(sock_fd, "SUBSCRIBE\n", 10, 0) < 0) {
        log_debug("SUBSCRIBE 전송 실패");
        goto retry;
    }
    n = recv(sock_fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0 || strncmp(buf, "OK", 2)) {
        log_debug("SUBSCRIBE 응답 오류");
        goto retry;
    }
    log_info("wlinkd 구독됨");

    pfd.fd = sock_fd;
    pfd.events = POLLIN;

    for (;;) {
        int ret = poll(&pfd, 1, 10000);
        if (ret < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (ret == 0)
            continue;
        if (pfd.revents & (POLLHUP | POLLERR)) {
            log_debug("연결 끊김, 재연결");
            goto retry;
        }
        if (pfd.revents & POLLIN) {
            n = recv(sock_fd, buf, sizeof(buf) - 1, 0);
            if (n <= 0) {
                log_debug("recv 실패, 재연결");
                goto retry;
            }
            buf[n] = '\0';
            if (!strncmp(buf, "sta_join", 8) ||
                !strncmp(buf, "sta_leave", 9) ||
                !strncmp(buf, "traffic", 7)) {
                char *nl = strchr(buf, '\n');
                if (nl)
                    *nl = '\0';
                led_fd = open("/dev/toy_simple_io_driver", O_WRONLY);
                if (led_fd >= 0) {
                    write(led_fd, buf, strlen(buf));
                    close(led_fd);
                }
                log_debug("buf:%s", buf);
            }
        }
    }
    if (sock_fd >= 0)
        close(sock_fd);

    return NULL;
}

static void *button_watcher_thread()
{
    if (g_debug_level == LOG_DEBUG) {
        pthread_mutex_lock(&print_lock);
        log_debug("button watcher thread started");
        pthread_mutex_unlock(&print_lock);
    }

    while (1) {
        if (g_wps_requested) {
            g_wps_requested = 0;
            pid_t pid = fork();

            log_info("버튼 눌림: WPS PBC 실행 요청 수신");
            if (pid == 0) {
                execl("/usr/sbin/hostapd_cli", "hostapd_cli", "-i", "wlan0", "wps_pbc", NULL);
                // execl("/usr/sbin/hostapd_cli", "hostapd_cli", "-i", "wlan0", "wps_pin", "any", "54321", NULL);
                _exit(1);
            } else if (pid > 0) {
                waitpid(pid, NULL, WNOHANG);
                log_debug("hostapd_cli가 SIGCHLD(17)를 보냈는지 확인할 것");
            }
        }
        usleep(100000);
    }

    return NULL;
}

/* 6.3.1. 락과 뮤텍스 */
void signal_exit(void)
{
    /* 6.3.2 멀티 스레드 동기화 & C와 C++ 연동, 종료 메세지를 보내도록 */
    pthread_mutex_lock(&system_loop_mutex);
    system_loop_exit = true;
    // pthread_cond_broadcast(&system_loop_cond);
    pthread_cond_signal(&system_loop_cond);
    pthread_mutex_unlock(&system_loop_mutex);

    return;
}

/* 버튼 인터럽트 드라이버(kdt_interrupt_driver)가 보내는 시그널 번호 */
#define BUTTON_SIGNR SIGUSR1

static void wps_signal_handler(int sig)
{
    (void)sig;
    g_wps_requested = 1;
}

int system_server()
{
    /* 6.2.5. 스레드 */
    int retcode;
    pthread_t wTid, mTid, dTid, tTid, eTid, lwTid, bwTid;

    if (g_debug_level == LOG_DEBUG) {
        pthread_mutex_lock(&print_lock);
        log_info("나 system_server 프로세스!");
        pthread_mutex_unlock(&print_lock);
    }

    /* 이 자식 프로세스에서 SIGUSR1 핸들러 등록 */
    {
        struct sigaction sa_btn;
        memset(&sa_btn, 0, sizeof(sa_btn));
        sa_btn.sa_handler = wps_signal_handler;
        sigemptyset(&sa_btn.sa_mask);
        sa_btn.sa_flags = SA_RESTART;
        sigaction(BUTTON_SIGNR, &sa_btn, NULL);
    }

    /* 버튼 드라이버에 현재(자식) 프로세스의 PID를 시그널 수신자로 등록 */
    {
        int btn_fd;
        char tmp[4];
        btn_fd = open("/dev/kdt_interrupt_driver", O_RDONLY);
        if (btn_fd >= 0) {
            read(btn_fd, tmp, sizeof(tmp));
            close(btn_fd);
            log_info("system_server: 버튼 드라이버에 PID %d 등록", (int)getpid());
        }
    }

    /* 6.3.4. POSIX 메시지 큐 */
    watchdog_queue = mq_open("/watchdog_queue", O_RDWR);
    assert(watchdog_queue != -1);
    monitor_queue = mq_open("/monitor_queue", O_RDWR);
    assert(monitor_queue != -1);
    disk_queue = mq_open("/disk_queue", O_RDWR);
    assert(disk_queue != -1);

    /* 6.2.5. 스레드 */
    retcode = pthread_create(&wTid, NULL, watchdog_thread, "watchdog thread");
    assert(retcode == 0);
    retcode = pthread_create(&mTid, NULL, monitor_thread, "monitor thread");
    assert(retcode == 0);
    retcode = pthread_create(&dTid, NULL, disk_service_thread, "disk service thread");
    assert(retcode == 0);
    retcode = pthread_create(&tTid, NULL, timer_thread, "timer thread");
    assert(retcode == 0);
    retcode = pthread_create(&eTid, NULL, engine_thread, "engine thread");
    assert(retcode == 0);
    retcode = pthread_create(&lwTid, NULL, led_watcher_thread, NULL);
    assert(retcode == 0);
    retcode = pthread_create(&bwTid, NULL, button_watcher_thread, NULL);
    assert(retcode == 0);

    if (g_debug_level == LOG_DEBUG) {
        pthread_mutex_lock(&print_lock);
        log_info("system init done.  waiting...");
        pthread_mutex_unlock(&print_lock);
    }

    // 여기서 cond wait로 대기한다. 10초 후 알람이 울리면 <== system 출력
    pthread_mutex_lock(&system_loop_mutex);
    while (system_loop_exit == false) {
        pthread_cond_wait(&system_loop_cond, &system_loop_mutex);
    }
    pthread_mutex_unlock(&system_loop_mutex);

    
    if (g_debug_level == LOG_DEBUG) {
        pthread_mutex_lock(&print_lock);
        log_info("<== system");
        pthread_mutex_unlock(&print_lock);
    }


    while (1) {
        sleep(1);
    }

    return 0;
}

int create_system_server()
{
    /* 6.2.2. 프로세스 관련 시스템 콜 */
    pid_t systemPid;
    const char *name = "system_server";

    log_info("여기서 시스템 프로세스를 생성합니다.");

    systemPid = fork();
    if (!systemPid) {
        if (prctl(PR_SET_NAME, (unsigned long)name) < 0)
            log_error("prctl() error: %s", strerror(errno));

        system_server();
    } else if (systemPid > 0) {
        ;
    } else {
        log_error("fork() failed!!");
    }

    return 0;
}
