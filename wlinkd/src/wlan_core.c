/*
 * wlinkd Domain/Service 계층
 * Copyright (c) 2026, Jaewon Park <pf0119@gmail.com>
 *
 * This software may be distributed under the terms of the BSD license.
 * See LICENSE for more details.
 */

#include <net/if.h>
#include <string.h>
#include <stdio.h>

#include "debug.h"
#include "wlan_core.h"
#include "wlan_libnl.h"
#include "wlan_nl80211.h"
#include "wlan_sta_mgr.h"
#include "wlan_utils.h"

static char g_wlan_core_ifname[IFNAMSIZ];

/*
 * wlan_core_init: wlan_nl80211.c, wlan_libnl.c 요소 초기화
 * 성공 시 이벤트 poll fd(실패 시 -1) 반환
 */
int wlan_core_init(const char *ifname)
{
    int fd;

    strncpy(g_wlan_core_ifname, ifname, IFNAMSIZ - 1);
    wlan_sta_mgr_init();

    fd = wlan_nl80211_init(ifname);
    if (fd < 0) {
        log_error("wlan_nl80211 초기화 실패");
        return -1;
    }

    return fd;
}

int wlan_core_get_fd(void)
{
    return wlan_libnl_get_fd();
}

int wlan_core_is_ap_mode(void)
{
    return wlan_nl80211_is_ap_mode();
}

int wlan_core_get_ap_info(ApInfo *info)
{
    return wlan_nl80211_get_ap_basic_info(info);
}

/* 이벤트 소켓에 들어온 nl80211 메시지를 libnl dispatch으로 처리 */
void wlan_core_handler(void)
{
    wlan_libnl_dispatch();
}

/* 타이머 주기로 호출되는 상태 갱신: AP 정보·STA 목록 갱신 */
void wlan_core_refresh(void)
{
    wlan_nl80211_refresh();
}

/*
 * build_status: refresh는 타이머 기반으로 호출되므로
 * 여기서는 g_req_sock 접근 없이 캐시된 상태만 읽는다.
 */
void wlan_core_build_status(char *buf, size_t size)
{
    ApInfo ap_info;
    Station *curr = sta_mgr_head;
    int pos = 0;
    int n;

    memset(&ap_info, 0, sizeof(ap_info));
    if (wlan_nl80211_get_ap_basic_info(&ap_info) < 0) {
        strncpy(ap_info.iface, g_wlan_core_ifname, sizeof(ap_info.iface) - 1);
        strncpy(ap_info.mode, WLAN_MODE, sizeof(ap_info.mode) - 1);
        strncpy(ap_info.ip, WLAN_NOT_AVAILABLE, sizeof(ap_info.ip) - 1);
        strncpy(ap_info.channel, WLAN_NOT_AVAILABLE, sizeof(ap_info.channel) - 1);
    }

    n = snprintf(buf, size,
                 "===== AP =====\nMODE=%s\nIFACE=%s\nIP=%s\nCHANNEL=%s\n"
                 "BANDWIDTH=%d MHz\nPHY_RATE=%d Mbps\nRSSI=%d dBm\nSTA_COUNT=%d\n",
                 ap_info.mode, ap_info.iface, ap_info.ip, ap_info.channel,
                 ap_info.bandwidth_mhz, ap_info.phy_rate_mbps,
                 ap_info.rssi_dbm, wlan_sta_mgr_count());
    if (n < 0 || (size_t)n >= size)
        return;
    pos += n;

    n = snprintf(buf + pos, size - pos,
                 "===== STA =====\nCOUNT=%d\n", wlan_sta_mgr_count());
    if (n < 0 || (size_t)n >= size - pos)
        return;
    pos += n;

    while (curr && (size_t)pos < size) {
        n = snprintf(buf + pos, size - pos,
                     "-----\nBSSID=%s\nRSSI=%d\nPKTS=%lu\nCONNECTED=%ld\n",
                     curr->bssid, curr->rssi, curr->priv_pkts,
                     (long)curr->assoc_time);
        if (n < 0 || (size_t)n >= size - pos)
            break;
        pos += n;
        curr = curr->next;
    }
}

/* 종료 시 리소스 해제: nl80211 다음 libnl 소켓이 해제됨 */
void wlan_core_close(void)
{
    wlan_nl80211_deinit();
    wlan_sta_mgr_clear();
}