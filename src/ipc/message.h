/*
 * Based on KDT Linux Expert Course.
 * Structure for messages sent through POSIX message queues
 * Copyright (c) 2026, Jaewon Park <pf0119@gmail.com>
 *
 * This software may be distributed under the terms of the BSD license.
 * See LICENSE for more details.
 */

#ifndef MESSAGE_H
#define MESSAGE_H

#include <unistd.h>

typedef struct {
    unsigned int msg_type;
    unsigned int param1;
    unsigned int param2;
    void *param3;
} mq_msg_t;

#endif /* MESSAGE_H */
