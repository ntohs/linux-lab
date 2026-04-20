/*
 * wlinkd Domain/Service 계층 - 정의
 * Copyright (c) 2026, Jaewon Park <pf0119@gmail.com>
 *
 * This software may be distributed under the terms of the BSD license.
 * See LICENSE for more details.
 */

#ifndef WLAN_CORE_H
#define WLAN_CORE_H

#include "wlan_utils.h"

/*
 * main.c 는 이 헤더만 포함해서 wlan_core API 만 호출한다.
 * wlan_libnl / wlan_nl80211 심볼은 main.c 가 몰라도 된다.
 */
int  wlan_core_init(const char *ifname);
int  wlan_core_get_fd(void);
int  wlan_core_is_ap_mode(void);
int  wlan_core_get_ap_info(ApInfo *info);
void wlan_core_handler(void);
void wlan_core_refresh(void);
void wlan_core_build_status(char *buf, size_t size);
void wlan_core_close(void);

#endif /* WLAN_CORE_H */
