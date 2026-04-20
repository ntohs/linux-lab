/*
 * STA management by linked list - definitions
 * Copyright (c) 2026, Jaewon Park <pf0119@gmail.com>
 *
 * This software may be distributed under the terms of the BSD license.
 * See LICENSE for more details.
 */

#ifndef WLAN_STA_MGR_H
#define WLAN_STA_MGR_H

#include "wlan_utils.h"

extern Station *sta_mgr_head;

/* sta_mgr_head 변수가 extern이므로 inline 써도 됨 */
static inline void wlan_sta_mgr_init()
{
    sta_mgr_head = NULL;
}

void wlan_sta_mgr_clear();
int wlan_sta_mgr_add(const char *bssid);
int wlan_sta_mgr_remove(const char *bssid);
int wlan_sta_mgr_count();
void wlan_sta_mgr_sync_from_list(const char bssids[][MAC_ADDR_LEN], int count);
void wlan_sta_mgr_list_print();
void wlan_sta_mgr_list_debug();
Station *wlan_sta_mgr_find(const char *bssid);

/*
 *  wlan_sta_mgr_join()  : "sta_join"  — STA 연결 시
 *  wlan_sta_mgr_leave() : "sta_leave" — 마지막 STA 해제
 *  wlan_sta_mgr_traffic()   : "traffic"  — STA 트래픽 감지
 */
void wlan_sta_mgr_join(const char *bssid);
void wlan_sta_mgr_leave(const char *bssid);
void wlan_sta_mgr_led_off();
void wlan_sta_mgr_traffic(const char *bssid);

#endif /* WLAN_STA_MGR_H */