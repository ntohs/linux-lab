/*
 * Based on KDT Linux Expert Course.
 * Lunchers for sub-processes
 * Copyright (c) 2026, Jaewon Park <pf0119@gmail.com>
 *
 * This software may be distributed under the terms of the BSD license.
 * See LICENSE for more details.
 */

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <wait.h>

#include "debug.h"
#include "input.h"
#include "launcher.h"
#include "worker.h"

#define WLINKD_PATH "./wlinkd/wlinkd"

static void prepare_project_root(void)
{
    static int initialized;
    char exe_path[256];
    ssize_t len;
    char *slash;

    if (initialized)
        return;

    initialized = 1;
    len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len <= 0)
        return;

    exe_path[len] = '\0';
    slash = strrchr(exe_path, '/');
    if (!slash)
        return;

    *slash = '\0';
    if (chdir(exe_path) != 0)
        log_info("프로젝트 루트로 이동하지 못해 현재 작업 디렉터리에서 계속 진행합니다.: %s",
                 strerror(errno));
}

int launch_wlinkd_daemon(void)
{
    pid_t system_pid;

    prepare_project_root();
    system("pkill -f './wlinkd/wlinkd' >/dev/null 2>&1 || true");

    if (access(WLINKD_PATH, X_OK) != 0) {
        log_error("wlinkd 실행 파일이 없어 실행을 건너뜁니다.");
        return 0;
    }

    log_info("wlinkd 프로세스를 시작합니다.");

    system_pid = fork();
    if (!system_pid) {
        char *log_args[] = {NULL, "-d", "-dd"};
        int log_argc = 0;
        prctl(PR_SET_PDEATHSIG, SIGTERM);

        if (g_debug_level == LOG_INFO)
            log_argc = 1;
        else if (g_debug_level == LOG_DEBUG)
            log_argc = 2;

        if (execl(WLINKD_PATH, WLINKD_PATH, "-s", log_args[log_argc], NULL)) {
            log_error("wlinkd 실행에 실패했습니다. %s", strerror(errno));
            exit(EXIT_FAILURE);
        }
    } else if (system_pid > 0) {
        log_info("wlinkd fork (PID: %d)", system_pid);
        return system_pid;
    } else {
        log_error("wlinkd fork failed %s", strerror(errno));
        return -1;
    }

    return 0;
}

