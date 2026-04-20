/*
 * Wireless LAN utilities
 * Copyright (c) 2026, Jaewon Park <pf0119@gmail.com>
 *
 * This software may be distributed under the terms of the BSD license.
 * See LICENSE for more details.
 */

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include "debug.h"
#include "wlan_utils.h"

/* IP정보 가져오기 */
void wlan_utils_fill_ip_address(const char *ifname, ApInfo *info)
{
    struct ifaddrs *ifaddr;
    struct ifaddrs *ifa;

    /* IPv4 주소는 libnl에 없어서 getifaddrs() 함수로 가져옴 */
    if (!ifname || !info || getifaddrs(&ifaddr) != 0)
        return;

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (!ifa->ifa_name || !ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
            continue;

        if (!strcmp(ifa->ifa_name, ifname)) {
            struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
            inet_ntop(AF_INET, &addr->sin_addr, info->ip, sizeof(info->ip));
            info->up = (ifa->ifa_flags & IFF_UP) ? 1 : 0;
            break;
        }
    }

    freeifaddrs(ifaddr);
}

/* STA MAC 바이트를 사람이 읽기 쉬운 문자열로 바꾸기 */
void wlan_utils_format_bssid(const unsigned char *bssid, char *buffer, size_t size)
{
    if (!bssid || !buffer || size < MAC_ADDR_LEN)
        return;

    snprintf(buffer, size,
             //"%02x:%02x:%02x:%02x:%02x:%02x",
             "%02X:%02X:%02X:%02X:%02X:%02X",
             bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
}

/* 주파수로 채널 문자열 구하기 */
void wlan_utils_format_channel(unsigned int freq, char *buffer, size_t size)
{
    if (!buffer || size == 0)
        return;

    /* under Wi-Fi 5 */
    if (freq == 2484)
        snprintf(buffer, size, "CH 14");
    else if (freq >= 2412 && freq <= 2472)
        snprintf(buffer, size, "CH %u", (freq - 2407) / 5);
    else if (freq >= 5000 && freq <= 5895)
        snprintf(buffer, size, "CH %u", (freq - 5000) / 5);
    else
        snprintf(buffer, size, "%u MHz", freq);
}

int wlan_utils_find_sta(const char *bssid, const char bssids[][MAC_ADDR_LEN], int count)
{
    int i;

    if (!bssids || !bssid) {
        log_error("함수를 잘 쓰자");
        return 0;
    }

    if (count <= 0)
        return 0;

    for (i = 0; i < count; i++) {
        if (!strcmp(bssid, bssids[i]))
            return 1;
    }

    return 0;
}
