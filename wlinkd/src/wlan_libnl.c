/*
 * Wireless LAN information using libnl
 * Copyright (c) 2026, Jaewon Park <pf0119@gmail.com>
 *
 * This software may be distributed under the terms of the BSD license.
 * See LICENSE for more details.
 */

#include <errno.h>
#include <linux/netlink.h>
#include <netlink/msg.h>
#include <netlink/netlink.h>
#include <sys/socket.h>

#include "debug.h"
#include "wlan_libnl.h"

/*
 * 소켓을 분리해서 이벤트와 요청 응답이 섞이는 문제를 방지
 * g_event_sock: mlme 멀티캐스트 이벤트 수신 전용 (poll 대상)
 * g_req_sock  : GET_INTERFACE / GET_STATION 요청/응답 전용
 */
struct nl_sock *g_event_sock = NULL; /* 멀티캐스트 이벤트 수신 전용 */
struct nl_sock *g_req_sock = NULL;   /* 유니캐스트 요청/응답 전용 */
int g_libnl_fd = -1;

static int wlan_libnl_ack_cb(struct nl_msg *msg, void *arg)
{
    int *err = (int *)arg;

    (void)msg;
    *err = 0;
    return NL_STOP;
}

static int wlan_libnl_finish_cb(struct nl_msg *msg, void *arg)
{
    int *err = (int *)arg;

    (void)msg;
    *err = 0;
    return NL_SKIP;
}

static int wlan_libnl_error_cb(struct sockaddr_nl *addr, struct nlmsgerr *err, void *arg)
{
    int *ret = (int *)arg;

    (void)addr;
    *ret = err->error;
    return NL_STOP;
}

/*
 * libnl 요청/응답 공통 처리 (from hostapd)
 * - g_req_sock 유니캐스트 전용으로 이벤트 메시지가 섞이지 않음
 * - NETLINK_EXT_ACK / CAP_ACK 설정으로 ACK 페이로드 최소화
 * - NLE_DUMP_INTR 감지 시 -EAGAIN 반환해 호출자가 재시도할 수 있도록 함
 */
int wlan_libnl_send_and_recv(struct nl_msg *msg, nl_recvmsg_msg_cb_t valid_cb, void *arg)
{
    struct nl_cb *cb;
    int err = -ENOMEM, opt;

    if (!g_req_sock || !msg) {
        nlmsg_free(msg);
        return -1;
    }

    cb = nl_cb_alloc(NL_CB_DEFAULT);
    if (!cb)
        goto out;

    /* NETLINK_EXT_ACK를 1로 설정 — 실패해도 무시 */
    opt = 1;
    setsockopt(nl_socket_get_fd(g_req_sock), SOL_NETLINK,
               NETLINK_EXT_ACK, &opt, sizeof(opt));

    /* NETLINK_CAP_ACK를 1로 설정 — 실패해도 무시 */
    opt = 1;
    setsockopt(nl_socket_get_fd(g_req_sock), SOL_NETLINK,
               NETLINK_CAP_ACK, &opt, sizeof(opt));

    err = nl_send_auto_complete(g_req_sock, msg);
    if (err < 0) {
        log_info("nl_send_auto_complete() failed: %s", nl_geterror(err));
        err = -EBADF;
        goto out;
    }

    err = 1;

    nl_cb_err(cb, NL_CB_CUSTOM, wlan_libnl_error_cb, &err);
    nl_cb_set(cb, NL_CB_FINISH, NL_CB_CUSTOM, wlan_libnl_finish_cb, &err);
    nl_cb_set(cb, NL_CB_ACK, NL_CB_CUSTOM, wlan_libnl_ack_cb, &err);
    if (valid_cb)
        nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, valid_cb, arg);

    while (err > 0) {
        int res = nl_recvmsgs(g_req_sock, cb);

        if (res == -NLE_DUMP_INTR) {
            /* dump 도중 내부 상태가 바뀌면 -EAGAIN 으로 변환해 호출자가 재시도 */
            log_error("nl80211: %s; convert to -EAGAIN", nl_geterror(res));
            err = -EAGAIN;
        } else if (res < 0) {
            log_info("nl80211: %s->nl_recvmsgs failed: %d (%s)",
                     __func__, res, nl_geterror(res));
        }
    }
out:
    nl_cb_put(cb);
    nlmsg_free(msg);
    return err;
}

int wlan_libnl_get_fd()
{
    return g_libnl_fd;
}

/* g_event_sock 에서 대기 중인 netlink 메시지를 읽어 등록된 콜백으로 처리 */
void wlan_libnl_dispatch()
{
    if (!g_event_sock)
        return;
    (void)nl_recvmsgs_default(g_event_sock);
}

/* 종료 시 libnl 소켓 두 개를 해제하고 상태를 초기화 */
void wlan_libnl_close()
{
    if (g_event_sock)
        nl_socket_free(g_event_sock);
    if (g_req_sock)
        nl_socket_free(g_req_sock);

    g_event_sock = NULL;
    g_req_sock = NULL;
    g_libnl_fd = -1;
}
