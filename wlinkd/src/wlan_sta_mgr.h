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

void wlan_sta_mgr_init();
void wlan_sta_mgr_clear();
int wlan_sta_mgr_add(const char *bssid);
int wlan_sta_mgr_remove(const char *bssid);
int wlan_sta_mgr_count();
void wlan_sta_mgr_sync_from_list(const char bssids[][MAC_ADDR_LEN], int count);
void wlan_sta_mgr_list_print();
void wlan_sta_mgr_list_debug();
Station *wlan_sta_mgr_find(const char *bssid);

extern Station *sta_mgr_head;

#endif /* WLAN_STA_MGR_H */