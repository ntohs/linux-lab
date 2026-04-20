/*
 * Based on KDT Linux Expert Course.
 * Shared memory management
 * Copyright (c) 2026, Jaewon Park <pf0119@gmail.com>
 *
 * This software may be distributed under the terms of the BSD license.
 * See LICENSE for more details.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/shm.h>
#include <sys/types.h>

#include "debug.h"
#include "shm.h"

int shm_id[SHM_KEY_MAX - SHM_KEY_BASE];

void *toy_shm_create(int key, int size)
{
    log_debug("key:%d, size:%d", key, size);

    if (key < SHM_KEY_BASE || key >= SHM_KEY_MAX || size <= 0) {
        log_error("invalid argument-> key: %d, size: %d", key, size);
        return (void *)-1;
    }

    int key_offset = key - SHM_KEY_BASE;
    shm_id[key_offset] = shmget((key_t)key, size, 0666 | IPC_CREAT);

    if (shm_id[key_offset] == -1) {
        log_error("shmget error: %d, %s", errno, strerror(errno));
        return (void *)-1;
    }

    void *ptr;
    ptr = toy_shm_attach(shm_id[key_offset]);
    if (ptr == (void *)-1) {
        log_error("shm_attach error");
        return (void *)-1;
    }
    return ptr;
}

void *toy_shm_attach(int shmid)
{
    void *ptr;
    if (shmid < 0) {
        return (void *)-1;
    }
    ptr = shmat(shmid, (void *)0, 0);
    if (ptr == (void *)-1) {
        log_error("shmat error: %d, %s", errno, strerror(errno));
        return (void *)-1;
    }
    return ptr;
}

int toy_shm_detach(void *ptr)
{
    int ret;
    if (ptr == NULL) {
        return -1;
    }
    ret = shmdt((const void *)ptr);
    if (ret < 0) {
        log_error("shmdt error: %d, %s", errno, strerror(errno));
    }
    return ret;
}

int toy_shm_remove(int shmid)
{
    int ret;
    if (shmid <= 0) {
        return -1;
    }
    ret = shmctl(shmid, IPC_RMID, NULL);
    if (ret < 0) {
        log_error("shmctl(RMID) error: %d, %s", errno, strerror(errno));
    }
    return ret;
}

int toy_shm_get_keyid(int key)
{
    if (key < SHM_KEY_BASE || key >= SHM_KEY_MAX) {
        log_error("invalid argument-> key: %d", key);
        return -1;
    }
    return shm_id[key - SHM_KEY_BASE];
}
