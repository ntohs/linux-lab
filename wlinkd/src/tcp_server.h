/*
 * TCP STA managements TCP Server for wlinkd - definitions
 * Copyright (c) 2026, Jaewon Park <pf0119@gmail.com>
 *
 * This software may be distributed under the terms of the BSD license.
 * See LICENSE for more details.
 */

#ifndef TCP_SERVER_H
#define TCP_SERVER_H

#define TCP_SERVER_PORT 9000
#define TCP_SERVER_BACKLOG 5
#define TCP_SERVER_BUF 1024

void *tcp_server_thread(void *arg);
void tcp_server_push_event(const char *event, const char *bssid);

#endif /* TCP_SERVER_H */
