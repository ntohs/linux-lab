/*
 * IPC for wlinkd
 * Copyright (c) 2026, Jaewon Park <pf0119@gmail.com>
 *
 * This software may be distributed under the terms of the BSD license.
 * See LICENSE for more details.
 */

#include "ipc.h"

#include "debug.h"
#include "wlan_core.h"
#include "wlan_sta_mgr.h"
#include "wlan_utils.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static int g_server_fd = -1;
static char g_socket_path[sizeof(((struct sockaddr_un *)0)->sun_path)] = IPC_SOCKET_PATH;

/* SUBSCRIBE 명령으로 등록된 push 클라이언트 fd 목록 */
#define IPC_MAX_PUSH_CLIENTS 8
static int g_push_fds[IPC_MAX_PUSH_CLIENTS];
static int g_push_count = 0;

int ipc_init(const char *socket_path)
{
    struct sockaddr_un addr;

    if (g_server_fd >= 0)
        return g_server_fd;

    g_server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_server_fd < 0) {
        log_error("IPC 소켓 생성 실패: %s", strerror(errno));
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;

    if (socket_path && *socket_path)
        strncpy(g_socket_path, socket_path, sizeof(g_socket_path) - 1);

    strncpy(addr.sun_path, g_socket_path, sizeof(addr.sun_path) - 1);
    unlink(g_socket_path);

    if (bind(g_server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        log_error("IPC 소켓 bind 실패: %s", strerror(errno));
        close(g_server_fd);
        g_server_fd = -1;
        return -1;
    }

    if (listen(g_server_fd, 4) < 0) {
        log_error("IPC 소켓 listen 실패: %s", strerror(errno));
        close(g_server_fd);
        g_server_fd = -1;
        unlink(g_socket_path);
        return -1;
    }

    log_info("[+] wlinkd IPC 소켓 준비 완료: %s", g_socket_path);
    return g_server_fd;
}

/*
 * STA 트래픽 감지 시 linux-lab에 "traffic" 을 전송
 * 연결이 끊어진 클라이언트는 자동으로 목록에서 제거
 */
static void ipc_push_event(const char *msg, size_t len)
{
    int i, j;

    for (i = 0, j = 0; i < g_push_count; i++) {
        int fd = g_push_fds[i];
        if (fd < 0)
            continue;
        if (send(fd, msg, len, MSG_DONTWAIT) < 0) {
            log_debug("IPC: push 클라이언트 %d 연결 끊김", fd);
            close(fd);
        } else {
            g_push_fds[j++] = fd;
        }
    }
    g_push_count = j;
}

void ipc_push_sta_join(void)
{
    ipc_push_event("sta_join\n", 9);
}

void ipc_push_sta_leave(void)
{
    ipc_push_event("sta_leave\n", 10);
}

void ipc_push_traffic(void)
{
    ipc_push_event("traffic\n", 8);
}

int ipc_get_fd(void)
{
    return g_server_fd;
}

void ipc_close(void)
{
    int i;

    /* push 클라이언트 연결 종료 */
    for (i = 0; i < g_push_count; i++) {
        if (g_push_fds[i] >= 0)
            close(g_push_fds[i]);
    }
    g_push_count = 0;

    if (g_server_fd >= 0)
        close(g_server_fd);

    g_server_fd = -1;
    unlink(g_socket_path);
}

void ipc_handler(void)
{
    int client_fd;
    char req[256];
    char buf[IPC_BUFFER_SIZE] = {0};
    ssize_t n;
    log_debug("ipc handler start");

    client_fd = accept(g_server_fd, NULL, NULL);
    if (client_fd < 0) {
        if (errno != EINTR)
            log_error("IPC accept 실패: %s", strerror(errno));
        return;
    }

    n = recv(client_fd, req, sizeof(req) - 1, 0);
    if (n <= 0) {
        close(client_fd);
        return;
    }

    req[n] = '\0';

    /* linux-lab이 traffic push 알림을 구독
     * fd를 닫지 않고 유지하며 g_push_fds[]에 등록 */
    if (strncmp(req, "SUBSCRIBE", 9) == 0) {
        if (g_push_count < IPC_MAX_PUSH_CLIENTS) {
            g_push_fds[g_push_count++] = client_fd;
            send(client_fd, "OK\n", 3, 0);
            log_info("IPC: push 클라이언트 등록 (fd=%d, count=%d)", client_fd, g_push_count);
            return; /* fd 유지 */
        }
        send(client_fd, "ERR full\n", 9, 0);
        close(client_fd);
        return;
    }

    if (!strcmp(req, "WLAN_PING")) {
        snprintf(buf, sizeof(buf), "%s\n", wlan_core_is_ap_mode() ? "PONG AP" : "PONG MONITOR");
    } else if (!strcmp(req, "WLAN_DUMP")) {
        wlan_core_build_status(buf, sizeof(buf));
    } else {
        snprintf(buf, sizeof(buf), "ERR unknown command: %s\n", req);
    }

    if (send(client_fd, buf, strlen(buf), 0) < 0)
        log_error("IPC 응답 전송 실패: %s", strerror(errno));

    close(client_fd);
    log_debug("ipc handler end");
}
