/*
 * STA managements TCP Server (9000 port) for wlinkd
 * usage at client: nc localhost 9000
 *  - command: status, monitor, exit
 *
 * Copyright (c) 2026, Jaewon Park <pf0119@gmail.com>
 * BSD License
 */

#include "tcp_server.h"
#include "debug.h"
#include "wlan_core.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

/*
 * Producer: tcp_server_push_event, Consumer: wlan_core's event push
 *
 * seq 카운터를 이용해 각 클라이언트가 자신의 last_seq를 독립적으로 관리
 * -> 한 클라이언트가 읽어도 다른 클라이언트의 대기에 영향 없음
 */

int g_running = 1;
char g_push_event[TCP_SERVER_BUF]; /* event push buffer */
pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t g_cond = PTHREAD_COND_INITIALIZER;
unsigned long long g_seq;

/* recv(), send() */
static void *client_handler(void *arg)
{
    int client_fd = *(int *)arg;
    free(arg);

    char req[TCP_SERVER_BUF];
    char resp[TCP_SERVER_BUF * 2];

    for (;;) {
        ssize_t n = recv(client_fd, req, sizeof(req) - 1, 0);
        if (n <= 0)
            break;

        req[n] = '\0';
        req[strcspn(req, "\r\n")] = '\0'; /* 줄바꿈 제거 */

        if (strcmp(req, "status") == 0) {
            wlan_core_build_status(resp, sizeof(resp));
            send(client_fd, resp, strlen(resp), 0);
        } else if (strcmp(req, "monitor") == 0) {
            unsigned long long last_seq;

            pthread_mutex_lock(&g_lock);
            last_seq = g_seq; /* 현재 seq 기억: 이후 이벤트만 수신 */
            pthread_mutex_unlock(&g_lock);

            send(client_fd, "Now monitoring (press Enter to stop)\n", 37, 0);

            for (;;) {
                char buf[TCP_SERVER_BUF];
                struct timespec ts;
                int rc;

                clock_gettime(CLOCK_REALTIME, &ts);
                ts.tv_sec += 1;

                pthread_mutex_lock(&g_lock);
                while (g_seq == last_seq) {
                    rc = pthread_cond_timedwait(&g_cond, &g_lock, &ts);
                    if (rc == ETIMEDOUT) {
                        log_debug("thread cond wake up. now check client cmd");
                        break;
                    }
                }

                /* is new event? */
                if (g_seq != last_seq) {
                    strncpy(buf, g_push_event, sizeof(buf) - 1);
                    buf[sizeof(buf) - 1] = '\0';
                    last_seq = g_seq;
                    pthread_mutex_unlock(&g_lock);

                    snprintf(resp, sizeof(resp), "event: %s\n", buf);
                    if (send(client_fd, resp, strlen(resp), 0) < 0)
                        break;
                } else {
                    pthread_mutex_unlock(&g_lock);
                }

                /* MSG_DONTWAIT: 데이터가 없으면 즉시 EAGAIN 리턴 (non-blocking) */
                ssize_t nr = recv(client_fd, req, sizeof(req) - 1, MSG_DONTWAIT);
                if (nr > 0) {
                    /* anykey */
                    send(client_fd, "Monitoring bye\n", sizeof("Monitoring bye") + 1, 0);
                    break;
                } else if (nr == 0 || (nr < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                    break;
                }
            }
        } else if (strcmp(req, "exit") == 0) {
            send(client_fd, "Bye\n", sizeof("Bye") + 1, 0);
            break;
        } else {
            snprintf(resp, sizeof(resp), "unknown cmmand: %s\n", req);
            send(client_fd, resp, strlen(resp), 0);
        }
    }

    close(client_fd);
    log_info("client disconnected");

    return NULL;
}

/* Figure 4.1 Socket functions for elementary TCP client/server.
 * socket() -> bind() -> listen() -> accept() (loop)
 * -> pthread_create + pthread_detach (연결마다)
 */
void *tcp_server_thread(void *arg)
{
    int server_fd;
    int opt = 1;
    struct sockaddr_in addr;

    (void)arg;

    /* 클라이언트 비정상 종료 시 프로세스 종료 방지 */
    signal(SIGPIPE, SIG_IGN);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return NULL;
    }

    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    addr.sin_family = AF_INET;
    addr.sin_port = htons(TCP_SERVER_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return NULL;
    }

    listen(server_fd, TCP_SERVER_BACKLOG);
    log_info("wlinkd TCP management ready: %d port", TCP_SERVER_PORT);

    /* accept 루프 */
    while (g_running) {
        struct sockaddr_in cli_addr;
        socklen_t cli_len = sizeof(cli_addr);
        int *pfd = NULL;
        int cli_fd = accept(server_fd, (struct sockaddr *)&cli_addr, &cli_len);

        if (cli_fd < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        log_info("client: %s", inet_ntoa(cli_addr.sin_addr));

        pfd = malloc(sizeof(int));
        *pfd = cli_fd;

        pthread_t tid;
        pthread_create(&tid, NULL, client_handler, pfd);
        pthread_detach(tid);
    }

    return NULL;
}

/* Producer API */
void tcp_server_push_event(const char *arg, const char *bssid)
{
    if (!bssid)
        log_error("bssid is NULL");

    pthread_mutex_lock(&g_lock);
    snprintf(g_push_event, sizeof(g_push_event) - 1, "%s %s", arg, bssid);
    g_seq++;                         /* new event */
    pthread_cond_broadcast(&g_cond); /* wake up all thread */
    pthread_mutex_unlock(&g_lock);

    return;
}
