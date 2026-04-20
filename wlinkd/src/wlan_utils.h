/*
 * Wireless LAN utilities - definitions
 * Copyright (c) 2026, Jaewon Park <pf0119@gmail.com>
 *
 * This software may be distributed under the terms of the BSD license.
 * See LICENSE for more details.
 */

#ifndef WLAN_UTILS_H
#define WLAN_UTILS_H

#include <net/if.h>
#include <time.h>

#define MAC_ADDR_LEN 18
#define WLAN_NOT_AVAILABLE "N/A"
#define WLAN_MODE "UNKNOWN"

typedef struct ap_info {
    char iface[32];
    char bssid[MAC_ADDR_LEN];
    char mode[16];
    char ip[32];
    char channel[32];
    int up;
    int bandwidth_mhz;
    int phy_rate_mbps;
    int rssi_dbm;
} ApInfo;

typedef struct station {
    char bssid[MAC_ADDR_LEN];
    char ip[16];
    int rssi;
    unsigned long priv_pkts;
    time_t assoc_time;
    struct station *next;
} Station;

void wlan_utils_format_bssid(const unsigned char *bssid, char *buffer, size_t size);
void wlan_utils_format_channel(unsigned int freq, char *buffer, size_t size);
void wlan_utils_fill_ip_address(const char *ifname, ApInfo *info);
int wlan_utils_find_sta(const char *bssid, const char bssids[][MAC_ADDR_LEN], int count);

#endif /* WLAN_UTILS_H */