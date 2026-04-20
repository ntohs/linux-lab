/*
 * Based on KDT Linux Expert Course.
 * System server process - definitions
 * Copyright (c) 2026, Jaewon Park <pf0119@gmail.com>
 *
 * This software may be distributed under the terms of the BSD license.
 * See LICENSE for more details.
 */

#ifndef WORKER_H
#define WORKER_H

#include <stdio.h>     /* Standard I/O functions */
#include <stdlib.h>    /* Prototypes of commonly used library functions,
                        * plus EXIT_SUCCESS and EXIT_FAILURE constants */
#include <errno.h>     /* Declares errno and defines error constants */
#include <stdbool.h>   /* 'bool' type plus 'true' and 'false' constants */
#include <string.h>    /* Commonly used string-handling functions */
#include <sys/types.h> /* Type definitions used by many programs */
#include <unistd.h>    /* Prototypes for many system calls */

int create_system_server();
int posix_sleep_ms(unsigned int timeout_ms);

#define USEC_PER_SECOND 1000000       /* one million */
#define USEC_PER_MILLISEC 1000        /* one thousand */
#define NANOSEC_PER_SECOND 1000000000 /* one BILLION */
#define NANOSEC_PER_USEC 1000         /* one thousand */
#define NANOSEC_PER_MILLISEC 1000000  /* one million */
#define MILLISEC_PER_TICK 10
#define MILLISEC_PER_SECOND 1000

#endif /* WORKER_H */
