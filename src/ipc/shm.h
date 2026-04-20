/*
 * Based on KDT Linux Expert Course.
 * Shared memory management - definitions
 * Copyright (c) 2026, Jaewon Park <pf0119@gmail.com>
 *
 * This software may be distributed under the terms of the BSD license.
 * See LICENSE for more details.
 */

#ifndef SHM_H
#define SHM_H

enum def_shm_key {
    SHM_KEY_BASE = 10,
    SHM_KEY_MAX
};

extern int shm_id[SHM_KEY_MAX - SHM_KEY_BASE];

void *toy_shm_create(int key, int size);
void *toy_shm_attach(int shmid);
int toy_shm_detach(void *ptr);
int toy_shm_remove(int shmid);
int toy_shm_get_keyid(int key);

#endif /* SHM_H */
