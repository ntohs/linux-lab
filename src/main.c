/*
 * Based on KDT Linux Expert Course.
 * linux-lab process
 * Copyright (c) 2026, Jaewon Park <pf0119@gmail.com>
 *
 * This software may be distributed under the terms of the BSD license.
 * See LICENSE for more details.
 */

#define _GNU_SOURCE

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "debug.h"
#include "input.h"
#include "launcher.h"
#include "worker.h"

/* 6.3.4. POSIX 메시지 큐 */
#include "message.h"
#include <mqueue.h>

#define NUM_MESSAGES 10
/* 버튼 인터럽트 드라이버(kdt_interrupt_driver)가 보내는 시그널 번호 */
#define BUTTON_SIGNR SIGUSR1

static mqd_t watchdog_queue;
static mqd_t monitor_queue;
static mqd_t disk_queue;

int g_wps_requested = 0;

/* 6.2.3. 시그널 */
static void signalHandler(int sig)
{
    int status, savedErrno;
    pid_t childPid;

    savedErrno = errno;

    log_debug("signalHandler param sig : %d", sig);

    while ((childPid = waitpid(-1, &status, WNOHANG)) > 0) {
        log_debug("handler: Reaped child %ld - ", (long)childPid);
        //(NULL, status);
    }

    if (childPid == -1 && errno != ECHILD)
        log_debug("waitpid");

    log_debug("signalHandler now ret");

    errno = savedErrno;

    return;
}

/* 6.3.4. POSIX 메시지 큐 */
int create_message_queue(mqd_t *mq_fd_ptr, const char *queue_name, int num_messages, int message_size)
{
    struct mq_attr attr;
    // int mq_errno
    mqd_t mq_fd;

    log_debug("func : %s, name : %s, msg_num : %d", __func__, queue_name, num_messages);

    memset(&attr, 0, sizeof(attr));
    attr.mq_msgsize = message_size;
    attr.mq_maxmsg = num_messages;

    mq_unlink(queue_name);
    mq_fd = mq_open(queue_name, O_RDWR | O_CREAT | O_CLOEXEC, 0777, &attr);
    if (mq_fd == -1) {
        log_error("func : %s, queue : %s already exists so try to open", __func__, queue_name);
        mq_fd = mq_open(queue_name, O_RDWR);
        assert(mq_fd != (mqd_t)-1);
        log_error("%s queue=%s opened successfully", __func__, queue_name);
        log_debug("But the program will end.");
        return -1;
    }

    *mq_fd_ptr = mq_fd;
    return 0;
}

void usage()
{
    fprintf(stderr,
            "\n"
            "usage: linux_lab [-hd] [-f <log file>]\n"
            "\\\n"
            "options:\n"
            "   -h   show this usage\n"
            "   -d   show more debug messages (-dd for even more)\n"
            "   -f   log output to debug file instead of stdout\n");

    exit(1);
}

int main(int argc, char *argv[])
{
    int c;
    /* 6.2.2. 프로세스 관련 시스템 콜 */
    pid_t spid, ipid, apid;
    int status, savedErrno;

    (void)savedErrno;

    for (;;) {
        c = getopt(argc, argv, "hdf:");
        if (c < 0)
            break;
        switch (c) {
        case 'h':
            usage();
            break;
        case 'f':
            set_log_file(optarg);
            log_error("경고: 라즈베리파이 용량 작아서 위험함!! TODO:적당히 저장");
            break;
        case 'd':
            ++g_debug_level;
            break;
        default:
            break;
        }
    }

    /* 6.2.3. 시그널 */
    // int sigCnt;
    if (signal(SIGCHLD, signalHandler) == SIG_ERR) {
        log_error("Could not signal user signal: %s", strerror(errno));
        abort();

        return 0;
    }

    /* 버튼 WPS 시그널 처리는 system_server 자식 프로세스에서 수행 (worker.c) */

    log_debug("메인 함수입니다.");

    /* 6.3.4. POSIX 메시지 큐 */
    int retcode;

    retcode = create_message_queue(&watchdog_queue, "/watchdog_queue", NUM_MESSAGES, sizeof(mq_msg_t));
    assert(retcode == 0);
    retcode = create_message_queue(&monitor_queue, "/monitor_queue", NUM_MESSAGES, sizeof(mq_msg_t));
    assert(retcode == 0);
    retcode = create_message_queue(&disk_queue, "/disk_queue", NUM_MESSAGES, sizeof(mq_msg_t));
    assert(retcode == 0);

    /* 6.2.2. 프로세스 관련 시스템 콜 */
    log_debug("메인 함수입니다.");
    log_debug("시스템 서버를 생성합니다.");
    spid = create_system_server();
    log_debug("입력 프로세스를 생성합니다.");
    ipid = create_input();
    log_debug("wlinkd를 실행합니다.");
    apid = launch_wlinkd_daemon();

    /* 버튼 드라이버에 현재 PID 등록은 system_server 자식 프로세스에서 수행 */

    if (spid > 0)
        waitpid(spid, &status, 0);
    if (ipid > 0)
        waitpid(ipid, &status, 0);
    if (apid > 0)
        waitpid(apid, &status, 0);

    return 0;
}
