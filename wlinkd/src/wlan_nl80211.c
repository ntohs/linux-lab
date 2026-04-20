/*
 * Wireless lan information using nl80211
 * Copyright (c) 2026, Jaewon Park <pf0119@gmail.com>
 *
 * This software may be distributed under the terms of the BSD license.
 * See LICENSE for more details.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <linux/nl80211.h>
#include <net/if.h>
#include <netlink/attr.h>
#include <netlink/genl/ctrl.h>
#include <netlink/genl/genl.h>
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "ipc.h"
#include "wlan_libnl.h"
#include "wlan_nl80211.h"
#include "wlan_sta_mgr.h"
#include "wlan_utils.h"

#define wlan_nl80211_MAX_STATIONS 64

/* 전방 선언 */
static void wlan_nl80211_update_sta(
    const char *bssid, int rssi_dbm, unsigned long total_pkts);
static void wlan_nl80211_remove_missing_stations(
    const char bssids[][MAC_ADDR_LEN], int count);

struct wlan_nl80211_sta_ctx {
    char (*bssids)[MAC_ADDR_LEN];
    int *count;
    int max_count;
};

static int g_nl80211_id = -1;
static int g_nl80211_is_ap_mode = 0;
static uint32_t g_wlan_idx = 0;
static char g_wlan_ifname[IFNAMSIZ];
static ApInfo g_ap_info;

int wlan_nl80211_is_ap_mode(void) { return g_nl80211_is_ap_mode; }

int wlan_nl80211_get_ap_basic_info(ApInfo *info)
{
    if (info == NULL)
        return -1;

    *info = g_ap_info;
    return 0;
}

/*
 * g_event_sock 에서 메시지를 읽어 wlan_nl80211_netdev_info_handler_cb 콜백으로
 * 처리 이벤트 수신만 담당 (AP 정보·STA 목록은 타이머 주기로
 * wlan_nl80211_refresh()가 담당)
 */
void wlan_nl80211_handler(void) { wlan_libnl_dispatch(); }

/*
 * GET_INTERFACE 응답과 MLME 이벤트(NEW/DEL_STATION)를 함께 처리
 * - GET_INTERFACE 응답: g_ap_info에 인터페이스 정보(모드, 채널, 대역폭) 저장
 * - MLME 이벤트: STA 연결/해제 로그와 sta_mgr 업데이트
 */
static int wlan_nl80211_netdev_info_handler_cb(struct nl_msg *msg, void *arg)
{
    struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
    struct nlattr *tb[NL80211_ATTR_MAX + 1];
    unsigned int ifindex;
    unsigned int iftype = NL80211_IFTYPE_UNSPECIFIED;
    char bssid[MAC_ADDR_LEN] = {0};

    (void)arg;
    memset(tb, 0, sizeof(tb));
    nla_parse(
        tb, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0),
        genlmsg_attrlen(gnlh, 0), NULL);

    if (!tb[NL80211_ATTR_IFINDEX])
        return NL_SKIP;

    /* 프로그램 인자로 받은 인터페이스인지 검사 */
    ifindex = nla_get_u32(tb[NL80211_ATTR_IFINDEX]);
    if (ifindex != g_wlan_idx)
        return NL_SKIP;

    if (tb[NL80211_ATTR_MAC])
        wlan_utils_format_bssid(
            nla_data(tb[NL80211_ATTR_MAC]), bssid, sizeof(bssid));

    /* MLME 이벤트 처리 */
    if (gnlh->cmd == NL80211_CMD_NEW_STATION) {
        if (bssid[0] && wlan_sta_mgr_add(bssid) > 0) {
            log_info("STA connected: %s", bssid);
            wlan_sta_mgr_list_debug();
            ipc_push_sta_join();
        }
        return NL_SKIP;
    } else if (gnlh->cmd == NL80211_CMD_DEL_STATION) {
        if (bssid[0]) {
            log_info("STA disconnected: %s", bssid);
            wlan_sta_mgr_remove(bssid);
            wlan_sta_mgr_list_debug();
            if (wlan_sta_mgr_count() == 0)
                ipc_push_sta_leave();
        }
        return NL_SKIP;
    }

    /* GET_INTERFACE 응답: 인터페이스 정보 갱신 */
    if (tb[NL80211_ATTR_IFNAME])
        strncpy(
            g_ap_info.iface, nla_get_string(tb[NL80211_ATTR_IFNAME]),
            sizeof(g_ap_info.iface) - 1);

    /* 채널 */
    if (tb[NL80211_ATTR_WIPHY_FREQ])
        wlan_utils_format_channel(
            nla_get_u32(tb[NL80211_ATTR_WIPHY_FREQ]), g_ap_info.channel,
            sizeof(g_ap_info.channel));

    /* 대역폭 */
    if (tb[NL80211_ATTR_CHANNEL_WIDTH])
        g_ap_info.bandwidth_mhz = wlan_nl80211_width_to_mhz(
            nla_get_u32(tb[NL80211_ATTR_CHANNEL_WIDTH]));

    /* 모드 */
    if (tb[NL80211_ATTR_IFTYPE]) {
        iftype = nla_get_u32(tb[NL80211_ATTR_IFTYPE]);
        if (iftype == NL80211_IFTYPE_AP ||
            iftype == NL80211_IFTYPE_P2P_GO) {
            strncpy(
                g_ap_info.mode, "AP", sizeof(g_ap_info.mode) - 1);
            g_nl80211_is_ap_mode = 1;
        } else if (iftype == NL80211_IFTYPE_MONITOR) {
            strncpy(
                g_ap_info.mode, "MONITOR",
                sizeof(g_ap_info.mode) - 1);
            g_nl80211_is_ap_mode = 0;
        } else {
            strncpy(
                g_ap_info.mode, "UNKNOWN",
                sizeof(g_ap_info.mode) - 1);
            g_nl80211_is_ap_mode = 0;
        }
    }

    wlan_utils_fill_ip_address(g_ap_info.iface, &g_ap_info);
    return NL_SKIP;
}

/* from hostapd (get_link_signal): station dump 응답에서 RSSI, bitrate, packet
 * 수를 읽어 STA 목록 등록과 활동 감지를 함께 처리 */
static int wlan_nl80211_get_link_signal_cb(struct nl_msg *msg, void *arg)
{
    struct wlan_nl80211_sta_ctx *ctx = (struct wlan_nl80211_sta_ctx *)arg;
    struct nlattr *tb[NL80211_ATTR_MAX + 1];
    struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
    struct nlattr *sinfo[NL80211_STA_INFO_MAX + 1];
    static struct nla_policy policy[NL80211_STA_INFO_MAX + 1] = {
        [NL80211_STA_INFO_SIGNAL] = {.type = NLA_U8},
        [NL80211_STA_INFO_SIGNAL_AVG] = {.type = NLA_U8},
        [NL80211_STA_INFO_BEACON_SIGNAL_AVG] = {.type = NLA_U8},
    };
    struct nlattr *rinfo[NL80211_RATE_INFO_MAX + 1];
    static struct nla_policy rate_policy[NL80211_RATE_INFO_MAX + 1] = {
        [NL80211_RATE_INFO_BITRATE] = {.type = NLA_U16},
        [NL80211_RATE_INFO_MCS] = {.type = NLA_U8},
        [NL80211_RATE_INFO_40_MHZ_WIDTH] = {.type = NLA_FLAG},
        [NL80211_RATE_INFO_SHORT_GI] = {.type = NLA_FLAG},
    };
    char bssid_text[MAC_ADDR_LEN] = {0};
    int rssi_dbm = 0;
    unsigned long total_packets = 0;

    memset(tb, 0, sizeof(tb));
    nla_parse(
        tb, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0),
        genlmsg_attrlen(gnlh, 0), NULL);

    if (!tb[NL80211_ATTR_MAC]) {
        //log_debug("get_link_signal_cb: no MAC attr — skip");
        return NL_SKIP;
    }

    /* BSSID를 먼저 확보하고 ctx에 등록 (remove_missing_stations 판단용) */
    wlan_utils_format_bssid(
        nla_data(tb[NL80211_ATTR_MAC]), bssid_text, sizeof(bssid_text));

    if (ctx && ctx->count && *ctx->count < ctx->max_count) {
        strncpy(ctx->bssids[*ctx->count], bssid_text, MAC_ADDR_LEN - 1);
        ctx->bssids[*ctx->count][MAC_ADDR_LEN - 1] = '\0';
        (*ctx->count)++;
    }

    /* STA_INFO 파싱 — 실패해도 BSSID는 이미 수집됨 */
    memset(sinfo, 0, sizeof(sinfo));
    if (!tb[NL80211_ATTR_STA_INFO]) {
        //log_debug("get_link_signal_cb: %s — no STA_INFO attr", bssid_text);
        goto update;
    }

    if (nla_parse_nested(
            sinfo, NL80211_STA_INFO_MAX, tb[NL80211_ATTR_STA_INFO], policy)) {
        //log_debug("get_link_signal_cb: %s — nested parse failed", bssid_text);
        goto update;
    }

    if (sinfo[NL80211_STA_INFO_SIGNAL])
        rssi_dbm = (int)(int8_t)nla_get_u8(sinfo[NL80211_STA_INFO_SIGNAL]);
    // else
    //     log_debug("get_link_signal_cb: %s — no SIGNAL attr", bssid_text);

    /* Kbps -> Mbps (BITRATE32 우선, 없으면 BITRATE 사용) */
    if (sinfo[NL80211_STA_INFO_TX_BITRATE]) {
        if (nla_parse_nested(
                rinfo, NL80211_RATE_INFO_MAX,
                sinfo[NL80211_STA_INFO_TX_BITRATE], rate_policy) == 0) {
            if (rinfo[NL80211_RATE_INFO_BITRATE32])
                g_ap_info.phy_rate_mbps =
                    (int)(nla_get_u32(
                              rinfo
                                  [NL80211_RATE_INFO_BITRATE32]) /
                          10U);
            else if (rinfo[NL80211_RATE_INFO_BITRATE])
                g_ap_info.phy_rate_mbps =
                    (int)(nla_get_u16(
                              rinfo
                                  [NL80211_RATE_INFO_BITRATE]) /
                          10U);
        } else {
            g_ap_info.phy_rate_mbps = 0;
        }
    }

    /* 패킷 개수 셈 */
    if (sinfo[NL80211_STA_INFO_TX_PACKETS])
        total_packets += (unsigned long)nla_get_u32(
            sinfo[NL80211_STA_INFO_TX_PACKETS]);
    if (sinfo[NL80211_STA_INFO_RX_PACKETS])
        total_packets += (unsigned long)nla_get_u32(
            sinfo[NL80211_STA_INFO_RX_PACKETS]);

update:
    g_ap_info.rssi_dbm = rssi_dbm;
    // log_debug("get_link_signal_cb: %s rssi=%d pkts=%lu",
    //           bssid_text, rssi_dbm, total_packets);

    wlan_nl80211_update_sta(bssid_text, rssi_dbm, total_packets);
    return NL_SKIP;
}

/* STA 를 등록하고 패킷 수 증가가 보이면 통신 중으로 판단 */
static void wlan_nl80211_update_sta(
    const char *bssid, int rssi_dbm, unsigned long total_pkts)
{
    int added;
    unsigned long priv_pkts = 0;
    Station *sta;

    if (!bssid || !*bssid)
        return;

    added = wlan_sta_mgr_add(bssid);
    sta = wlan_sta_mgr_find(bssid);
    if (!sta) {
        //log_error("update_sta: add succeeded but find failed for %s", bssid);
        return;
    }

    priv_pkts = sta->priv_pkts;
    sta->rssi = rssi_dbm;
    sta->priv_pkts = total_pkts;
    //log_debug("update_sta: %s rssi=%d pkts=%lu→%lu",
    //          bssid, rssi_dbm, priv_pkts, total_pkts);

    if (added > 0) {
        log_info(
            "STA connected: %s rssi=%d pkts=%lu", bssid, rssi_dbm,
            total_pkts);
        wlan_sta_mgr_list_debug();
        /* 새 STA 연결 - LED 켜짐 이벤트 전송 */
        ipc_push_sta_join();
    } else if (total_pkts > priv_pkts) {
        /* STA 트래픽 감지 - linux-lab에 push 알림 전송 */
        ipc_push_traffic();
        log_info(
            "STA active: %s rssi=%d pkts=%lu", bssid, rssi_dbm,
            total_pkts);
    }

    return;
}

/* station dump 결과에 없는 STA을 목록에서 제거 */
static void wlan_nl80211_remove_missing_stations(
    const char bssids[][MAC_ADDR_LEN], int count)
{
    Station *curr = sta_mgr_head;
    Station *next;
    int removed = 0;

    /* bssids가 NULL이면 전부 제거 (non-AP 전환 시) */
    if (!bssids || count < 0) {
        while (curr) {
            next = curr->next;
            log_info("STA disconnected (mode change): %s", curr->bssid);
            wlan_sta_mgr_remove(curr->bssid);
            removed++;
            curr = next;
        }
        if (removed > 0 && wlan_sta_mgr_count() == 0)
            ipc_push_sta_leave();
        return;
    }

    while (curr) {
        next = curr->next;
        if (!wlan_utils_find_sta(curr->bssid, bssids, count)) {
            log_info("STA disconnected: %s", curr->bssid);
            wlan_sta_mgr_remove(curr->bssid);
            wlan_sta_mgr_list_debug();
            removed++;
        }
        curr = next;
    }

    /* 마지막 STA 가 사라지면 LED 꺼짐 이벤트 전송 */
    if (removed > 0 && wlan_sta_mgr_count() == 0)
        ipc_push_sta_leave();
}

/* 채널 폭을 MHz로 변환 */
int wlan_nl80211_width_to_mhz(unsigned int width)
{
    switch (width) {
    case NL80211_CHAN_WIDTH_5:
        return 5;
    case NL80211_CHAN_WIDTH_10:
        return 10;
    case NL80211_CHAN_WIDTH_20_NOHT:
    case NL80211_CHAN_WIDTH_20:
        return 20;
    case NL80211_CHAN_WIDTH_40:
        return 40;
    case NL80211_CHAN_WIDTH_80:
        return 80;
    case NL80211_CHAN_WIDTH_80P80:
    case NL80211_CHAN_WIDTH_160:
        return 160;
    default:
        return 0;
    }
}

/* external functions */

/* libnl 소켓 두 개를 준비하고 이벤트 구독을 시작 */
int wlan_nl80211_init(const char *ifname)
{
    int mcid;

    strncpy(g_wlan_ifname, ifname, IFNAMSIZ - 1);
    g_wlan_idx = if_nametoindex(ifname);
    if (g_wlan_idx == 0) {
        log_error("%s 인터페이스를 찾을 수 없음", ifname);
        return -1;
    }

    wlan_nl80211_reset_ap_info();
    g_req_sock = nl_socket_alloc();
    if (!g_req_sock)
        return -1;

    if (genl_connect(g_req_sock) < 0) {
        wlan_libnl_close();
        return -1;
    }

    g_nl80211_id = genl_ctrl_resolve(g_req_sock, "nl80211");
    if (g_nl80211_id < 0) {
        log_error("nl80211 드라이버를 찾을 수 없음");
        wlan_libnl_close();
        return -1;
    }

    /* 이벤트 수신 전용 소켓: mlme 멀티캐스트 그룹 가입 */
    g_event_sock = nl_socket_alloc();
    if (!g_event_sock) {
        wlan_libnl_close();
        return -1;
    }

    if (genl_connect(g_event_sock) < 0) {
        wlan_libnl_close();
        return -1;
    }

    nl_socket_modify_cb(
        g_event_sock, NL_CB_VALID, NL_CB_CUSTOM,
        wlan_nl80211_netdev_info_handler_cb, NULL);

    mcid = genl_ctrl_resolve_grp(g_event_sock, "nl80211", "mlme");
    if (mcid >= 0)
        nl_socket_add_membership(g_event_sock, mcid);

    g_libnl_fd = nl_socket_get_fd(g_event_sock);
    wlan_nl80211_refresh();

    return g_libnl_fd;
}

void wlan_nl80211_deinit(void) { wlan_libnl_close(); }

/* AP 기본 정보 구조체를 초기값으로 되돌림 */
void wlan_nl80211_reset_ap_info(void)
{
    g_nl80211_is_ap_mode = 0;
    memset(&g_ap_info, 0, sizeof(g_ap_info));
    strncpy(g_ap_info.iface, g_wlan_ifname, sizeof(g_ap_info.iface) - 1);

    strncpy(g_ap_info.mode, WLAN_MODE, sizeof(g_ap_info.mode) - 1);
    strncpy(g_ap_info.ip, WLAN_NOT_AVAILABLE, sizeof(g_ap_info.ip) - 1);
    strncpy(
        g_ap_info.channel, WLAN_NOT_AVAILABLE,
        sizeof(g_ap_info.channel) - 1);
    wlan_utils_fill_ip_address(g_ap_info.iface, &g_ap_info);
}

/*
 * 현재 wlan 인터페이스가 AP 모드인지 확인하고,
 * AP 라면 station dump 로 연결 목록과 패킷 증가 여부를 갱신
 */
void wlan_nl80211_refresh(void)
{
    struct nl_msg *msg;
    struct wlan_nl80211_sta_ctx ctx;
    char station_bssids[wlan_nl80211_MAX_STATIONS][MAC_ADDR_LEN] = {{0}};
    int station_count = 0;

    if (!g_req_sock || g_nl80211_id < 0)
        return;

    wlan_nl80211_reset_ap_info();
    g_nl80211_is_ap_mode = 0;

    msg = nlmsg_alloc();
    if (!msg)
        return;

    genlmsg_put(
        msg, 0, 0, g_nl80211_id, 0, NLM_F_DUMP, NL80211_CMD_GET_INTERFACE,
        0);
    if (wlan_libnl_send_and_recv(
            msg, wlan_nl80211_netdev_info_handler_cb, NULL) < 0) {
        log_debug(
            "AP 기본 정보 조회 실패 (인터페이스 전환 중일 수 있음)");
        return;
    }

    if (!g_nl80211_is_ap_mode) {
        wlan_nl80211_remove_missing_stations(NULL, -1);
        return;
    }

    msg = nlmsg_alloc();
    if (!msg)
        return;

    genlmsg_put(
        msg, 0, 0, g_nl80211_id, 0, NLM_F_DUMP, NL80211_CMD_GET_STATION, 0);
    nla_put_u32(msg, NL80211_ATTR_IFINDEX, g_wlan_idx);

    memset(&ctx, 0, sizeof(ctx));
    ctx.bssids = station_bssids;
    ctx.count = &station_count;
    ctx.max_count = wlan_nl80211_MAX_STATIONS;

    if (wlan_libnl_send_and_recv(
            msg, wlan_nl80211_get_link_signal_cb, &ctx) < 0) {
        log_debug("station dump unavailable for %s — skip remove", g_wlan_ifname);
        return;
    }

    /* verbose */
    //log_debug("station dump done: %d STA(s) found", station_count);
    wlan_nl80211_remove_missing_stations(station_bssids, station_count);
}
