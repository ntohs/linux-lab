/*
 * wlinkd process
 * Copyright (c) 2026, Jaewon Park <pf0119@gmail.com>
 *
 * This software may be distributed under the terms of the BSD license.
 * See LICENSE for more details.
 */

#define _GNU_SOURCE

#include "debug.h"
#include "ipc.h"
#include "wlan_core.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t g_running = 1;

static void shutdown_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

static struct pollfd fds[3];
static int nfds = 0;

#define LED_DEVICE "/dev/toy_simple_io_driver"
#define LED_OFF "sta_leave"

static void led_off()
{
    int fd = open(LED_DEVICE, O_WRONLY);
    if (fd >= 0) {
        write(fd, LED_OFF, strlen(LED_OFF));
        close(fd);
    } else {
        log_error("open failed: %s", strerror(errno));
    }

    return;
}

void usage()
{
    fprintf(stderr,
            "\n"
            "usage: wlinkd [-hd] [-i <ifname>] [-s <socket path>]\n"
            "\\\n"
            "options:\n"
            "   -h   show this usage\n"
            "   -d   show more debug messages (-dd for even more)\n"
            "   -i   specify wlan interface (default: wlan0)\n"
            "   -s   specify IPC socket path\n");

    exit(1);
}

int main(int argc, char *argv[])
{
    int c;
    int ipc = 0;
    int ipc_idx = -1;
    int nl_idx = -1;
    int timer_idx = -1;
    struct itimerspec spec;
    int timer_fd;
    const char *ifname = "wlan0";

    while ((c = getopt(argc, argv, "hi:df:s")) != -1) {
        switch (c) {
        case 'h':
            usage(argv[0]);
            return 0;
        case 'i':
            ifname = optarg;
            break;
        case 'd':
            if (g_debug_level < LOG_DEBUG)
                ++g_debug_level;
            break;
        case 'f':
            set_log_file(optarg);
            break;
        case 's':
            ipc = 1;
            break;
        default:
            usage(argv[0]);
            return 1;
        }
    }

    /* SIGTERM / SIGINT / SIGHUP → LED를 끄고 정상종료 */
    {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = shutdown_handler;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGTERM, &sa, NULL);
        sigaction(SIGINT, &sa, NULL);
        sigaction(SIGHUP, &sa, NULL);
    }

    /* wlan_core_init 에서 wlan_sta_mgr_init 이 호출되므로 별도 초기화 불필요 */
    if (wlan_core_init(ifname) < 0) {
        log_error("[-] wlan_core 초기화 실패");
        return 1;
    }

    /*
     * ipc 플래그를 유지해서 wlinkd를 단독 실행할 수 있게 한다.
     * -s 옵션이 없으면 AF_UNIX 소켓도 만들지 않는다.
     */
    if (ipc && ipc_init(NULL) < 0) {
        log_error("[-] IPC 초기화 실패");
        wlan_core_close();
        return 1;
    }

    /* 타이머: 1초 주기로 wlan_core_refresh 호출해 AP 정보·STA 목록 갱신 */
    timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (timer_fd < 0)
        return -1;

    memset(&spec, 0, sizeof(spec));
    spec.it_interval.tv_sec = 1;
    spec.it_value.tv_sec = 1;
    timerfd_settime(timer_fd, 0, &spec, NULL);

    /* ipc */
    if (ipc) {
        ipc_idx = nfds;
        fds[nfds].fd = ipc_get_fd();
        fds[nfds++].events = POLLIN;
    }

    /* wlan_core */
    nl_idx = nfds;
    fds[nfds].fd = wlan_core_get_fd();
    fds[nfds++].events = POLLIN;

    /* wlan_core timer */
    if (timer_fd >= 0) {
        timer_idx = nfds;
        fds[nfds].fd = timer_fd;
        fds[nfds++].events = POLLIN;
    }

    if (ipc)
        log_info("[+] wlinkd 실행 중: IPC 사용, 소켓 경로: %s", IPC_SOCKET_PATH);
    else
        log_info("[+] wlinkd 실행 중: 단독 모드");

    /* 프로세스 시작 기본 상태이므로 LED는 끔 */
    led_off();

    while (g_running) {
        if (poll(fds, nfds, -1) < 0) {
            if (errno == EINTR)
                continue;
            break;
        }

        /* ipc */
        if (ipc_idx >= 0 && (fds[ipc_idx].revents & POLLIN))
            ipc_handler();

        /* wlan_core */
        if (fds[nl_idx].revents & POLLIN)
            wlan_core_handler();

        /* wlan_core timer */
        if (timer_idx >= 0 && (fds[timer_idx].revents & POLLIN)) {
            uint64_t expirations;
            if (read(timer_fd, &expirations, sizeof(expirations)) > 0)
                wlan_core_refresh();
        }
    }

    if (timer_fd >= 0)
        close(timer_fd);

    /* 종료 시 LED를 끔 */
    led_off();

    if (ipc)
        ipc_close();
    wlan_core_close();

    return 0;
}