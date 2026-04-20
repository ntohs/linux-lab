/*
 * IPC for wlinkd - definitions
 * Copyright (c) 2026, Jaewon Park <pf0119@gmail.com>
 *
 * This software may be distributed under the terms of the BSD license.
 * See LICENSE for more details.
 */

#ifndef IPC_H
#define IPC_H

#define IPC_SOCKET_PATH "/tmp/wlinkd.sock"
#define IPC_BUFFER_SIZE 4096

int ipc_init(const char *socket_path);
void ipc_push_event(const char *msg, int len);
int ipc_get_fd();
void ipc_close();
void ipc_handler();

#endif /* IPC_H */
