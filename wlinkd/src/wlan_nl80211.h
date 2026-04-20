/*
 * Wireless lan information using nl80211 - definitions
 * Copyright (c) 2026, Jaewon Park <pf0119@gmail.com>
 *
 * This software may be distributed under the terms of the BSD license.
 * See LICENSE for more details.
 */

#ifndef WLAN_NL80211_H
#define WLAN_NL80211_H

#include "wlan_utils.h"

int wlan_nl80211_init(const char *ifname);
void wlan_nl80211_deinit(void);
void wlan_nl80211_refresh(void);
void wlan_nl80211_handler(void);
void wlan_nl80211_reset_ap_info(void);
int wlan_nl80211_is_ap_mode(void);
int wlan_nl80211_get_ap_basic_info(ApInfo *info);
int wlan_nl80211_width_to_mhz(unsigned int width);

#endif /* WLAN_NL80211_H */