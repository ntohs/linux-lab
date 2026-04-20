/*
 * Wireless LAN information using libnl - definitions
 * Copyright (c) 2026, Jaewon Park <pf0119@gmail.com>
 *
 * This software may be distributed under the terms of the BSD license.
 * See LICENSE for more details.
 */

#ifndef WLAN_LIBNL_H
#define WLAN_LIBNL_H

#include <netlink/netlink.h>
#include <netlink/msg.h>

/*
 * wlan_nl80211.c 에서 직접 접근하는 소켓 전역변수.
 * g_req_sock  : GET_INTERFACE / GET_STATION 요청/응답 전용
 * g_event_sock: mlme 멀티캐스트 이벤트 수신 전용
 */
extern struct nl_sock *g_event_sock;
extern struct nl_sock *g_req_sock;
extern int g_libnl_fd;

int  wlan_libnl_get_fd(void);
int  wlan_libnl_send_and_recv(struct nl_msg *msg, nl_recvmsg_msg_cb_t valid_cb, void *arg);
void wlan_libnl_dispatch(void);
void wlan_libnl_close(void);

#endif /* WLAN_LIBNL_H */