/*
 * STA managemantent by linked list
 * Copyright (c) 2026, Jaewon Park <pf0119@gmail.com>
 *
 * This software may be distributed under the terms of the BSD license.
 * See LICENSE for more details.
 */

#include "wlan_sta_mgr.h"
#include "debug.h"
#include "wlan_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Station *sta_mgr_head = NULL;

/* 연결리스트로 STA 관리 */

void wlan_sta_mgr_init(void)
{
    sta_mgr_head = NULL;
}

void wlan_sta_mgr_clear(void)
{
    Station *curr = sta_mgr_head;

    while (curr) {
        Station *next = curr->next;
        log_debug("clear station: %s", curr->bssid);
        free(curr);
        curr = next;
    }

    sta_mgr_head = NULL;
}

int wlan_sta_mgr_add(const char *bssid)
{
    Station *new_sta;

    if (!bssid || !*bssid)
        return -1;

    if (wlan_sta_mgr_find(bssid))
        return 0;

    new_sta = (Station *)malloc(sizeof(Station));
    if (!new_sta)
        return -1;

    memset(new_sta, 0, sizeof(Station));
    strncpy(new_sta->bssid, bssid, MAC_ADDR_LEN - 1);
    new_sta->assoc_time = time(NULL);
    new_sta->next = sta_mgr_head;
    sta_mgr_head = new_sta;

    log_debug("Added station: %s", bssid);
    return 1;
}

int wlan_sta_mgr_remove(const char *bssid)
{
    Station *curr = sta_mgr_head;
    Station *prev = NULL;

    while (curr) {
        if (!strcmp(curr->bssid, bssid)) {
            if (prev)
                prev->next = curr->next;
            else
                sta_mgr_head = curr->next;
            log_debug("Removed station: %s", bssid);
            free(curr);
            return 1;
        }
        prev = curr;
        curr = curr->next;
    }
    return 0;
}

int wlan_sta_mgr_count(void)
{
    int count = 0;
    Station *curr = sta_mgr_head;

    while (curr) {
        count++;
        curr = curr->next;
    }

    return count;
}

void wlan_sta_mgr_sync_from_list(const char bssids[][MAC_ADDR_LEN], int count)
{
    int i;
    Station *curr;
    Station *next;

    for (i = 0; i < count; i++) {
        if (bssids[i][0] != '\0')
            wlan_sta_mgr_add(bssids[i]);
    }

    curr = sta_mgr_head;
    while (curr) {
        next = curr->next;
        if (!wlan_utils_find_sta(curr->bssid, bssids, count))
            wlan_sta_mgr_remove(curr->bssid);
        curr = next;
    }
}

Station *wlan_sta_mgr_find(const char *bssid)
{
    Station *curr = sta_mgr_head;

    while (curr) {
        if (!strcmp(curr->bssid, bssid))
            return curr;
        curr = curr->next;
    }
    return NULL;
}

void wlan_sta_mgr_list_print(void)
{
    Station *curr = sta_mgr_head;

    printf("\n--------- Current Connected Stations ---------\n");
    while (curr) {
        printf("MAC: %s | Connected: %s", curr->bssid, ctime(&curr->assoc_time));
        curr = curr->next;
    }
    printf("------------------------------------------------\n");
}

void wlan_sta_mgr_list_debug(void)
{
    Station *curr = sta_mgr_head;

    if (g_debug_level < LOG_DEBUG)
        return;

    log_debug("--------- Current Connected Stations ---------");
    while (curr) {
        char temp[256] = {0};

        strncpy(temp, ctime(&curr->assoc_time), sizeof(temp) - 1);
        temp[strlen(temp) - 1] = '\0';
        log_debug("MAC: %s | Connected: %s", curr->bssid, temp);
        curr = curr->next;
    }
    log_debug("----------------------------------------------");
}