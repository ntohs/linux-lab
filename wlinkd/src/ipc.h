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

int  ipc_init(const char *socket_path);
int  ipc_get_fd(void);
void ipc_close(void);
void ipc_handler(void);

/*
 * push 이벤트: SUBSCRIBE 명령으로 등록된 모든 클라이언트에 전송
 *  ipc_push_sta_join()  : "sta_join"  — STA 연결 시
 *  ipc_push_sta_leave() : "sta_leave" — 마지막 STA 해제
 *  ipc_push_traffic()   : "traffic"  — STA 트래픽 감지
 */
void ipc_push_sta_join(void);
void ipc_push_sta_leave(void);
void ipc_push_traffic(void);

#endif /* IPC_H */

