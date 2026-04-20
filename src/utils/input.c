/*
 * Based on KDT Linux Expert Course.
 * Input functionality for system server process
 * Copyright (c) 2026, Jaewon Park <pf0119@gmail.com>
 *
 * This software may be distributed under the terms of the BSD license.
 * See LICENSE for more details.
 */

#define _GNU_SOURCE

#include <errno.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/prctl.h>

#include "debug.h"
#include "input.h"
#include "launcher.h"
#include "worker.h"

#include <assert.h>
#include <execinfo.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <ucontext.h>
#include <unistd.h>

#include "message.h"
#include <mqueue.h>

#include "shm.h"
#include <fcntl.h>
#include <ucontext.h>

#include <sys/mman.h>
#include <sys/stat.h>

#define TOY_TOK_BUFSIZE 64
#define TOY_TOK_DELIM " \t\r\n\a"
#define TOY_BUFFSIZE 1024
#define DUMP_STATE 2

// #define PCP

/* 6.2.3. 시그널 */
typedef struct _sig_ucontext {
    unsigned long uc_flags;
    struct ucontext *uc_link;
    stack_t uc_stack;
    struct sigcontext uc_mcontext;
    sigset_t uc_sigmask;
} sig_ucontext_t;

/* 6.3.1 락과 뮤텍스 */
static pthread_mutex_t global_message_mutex = PTHREAD_MUTEX_INITIALIZER;
static char global_message[TOY_BUFFSIZE];

/* 6.3.4. POSIX 메시지 큐 */
static mqd_t watchdog_queue;
static mqd_t monitor_queue;
static mqd_t disk_queue;

/* 6.4.2. 시스템 V 메시지 큐 & 세마포어 & 공유 메모리 */

// 레퍼런스 코드
void segfault_handler(int sig_num, siginfo_t *info, void *ucontext)
{
    void *array[50];
    void *caller_address;
    char **messages;
    int size, i;
    sig_ucontext_t *uc;

    uc = (sig_ucontext_t *)ucontext;

    /* Get the address at the time the signal was raised */
    // rip: x86_64 specific, arm_pc: ARM, pc: ARM64
#if defined(__x86_64__)
    caller_address = (void *)uc->uc_mcontext.rip;
#elif defined(__arm__)
    caller_address = (void *)uc->uc_mcontext.arm_pc;
#elif defined(__aarch64__)
    caller_address = (void *)uc->uc_mcontext.pc;
#endif
    fprintf(stderr, "\n");

    if (sig_num == SIGSEGV)
        log_error("signal %d (%s), address is %p from %p", sig_num, strsignal(sig_num), info->si_addr, (void *)caller_address);
    else
        log_error("signal %d (%s)", sig_num, strsignal(sig_num));

    size = backtrace(array, 50);
    /* overwrite sigaction with caller's address */
    array[1] = caller_address;
    messages = backtrace_symbols(array, size);

    /* skip first stack frame (points here) */
    for (i = 1; i < size && messages != NULL; ++i) {
        log_debug("[bt]: (%d) %s", i, messages[i]);
    }

    free(messages);

    exit(EXIT_FAILURE);
}

/*
 *  command thread
 */
int toy_help(char **args);
int toy_wlan(char **args);
int toy_mutex(char **args);
int toy_shell(char **args);
int toy_exit(char **args);
int toy_elf(char **args);
int toy_dump_state(char **args);
int toy_mincore(char **args);
int toy_busy(char **args);

char *builtin_str[] = {
    "help",
    "wlan",
    "mu",
    "sh",
    "exit",
    "elf",
    "dump",
    "mincore",
    "busy"};

int (*builtin_func[])(char **) = {
    &toy_help,
    &toy_wlan,
    &toy_mutex,
    &toy_shell,
    &toy_exit,
    &toy_elf,
    &toy_dump_state,
    &toy_mincore,
    &toy_busy};

int toy_num_builtins()
{
    return sizeof(builtin_str) / sizeof(char *);
}

int toy_help(char **args)
{
    int i;

    for (i = 0; i < toy_num_builtins(); i++) {
        printf("%s\n", builtin_str[i]);
    }

    return 1;
}

int toy_wlan(char **args)
{
    int sd = 0;
    struct sockaddr_un addr;
    const char *msg = "WLAN_DUMP";
    const char *sock_path = "/tmp/wlinkd.sock";
    char buffer[1024] = {0};

    if (access(sock_path, F_OK) != 0) {
        log_error("wlinkd IPC 소켓이 없음...");
        return 1;
    }

    /* AF_UNIX: Unix Domain Socket*/
    sd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sd < 0) {
        log_error("socket: %s", strerror(errno));
        return 1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);

    if (connect(sd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        log_error("connect: %s", strerror(errno));
        close(sd);
        return 1;
    }
#if 0 /* test */
    msg = "WLAN_PONG";
#endif
    log_debug("send, 메시지:%s", msg);
    send(sd, msg, strlen(msg), 0);

    /* 주의: read의 반환이 0이하면 buffer가 NULL이라 프로세스 죽음 */
    if (read(sd, buffer, sizeof(buffer) - 1) > 0)
        printf("%s", buffer);

    close(sd);
    return 1;
}

int toy_mutex(char **args)
{
    if (args[1] == NULL) {
        return 1;
    }

    log_info("save message: %s", args[1]);
    pthread_mutex_lock(&global_message_mutex);
    strcpy(global_message, args[1]);
    pthread_mutex_unlock(&global_message_mutex);
    return 1;
}

int toy_exit(char **args)
{
    (void)args;
    return 0;
}

int toy_elf(char **args)
{
    int fd;
    size_t buf_size;
    struct stat st;
    Elf64Hdr *map;
    const char *path = "/proc/self/exe";

    (void)args;

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        log_error("cannot open %s", path);
        return 1;
    }

    if (!fstat(fd, &st)) {
        buf_size = st.st_size;
        if (!buf_size) {
            log_error("%s is empty", path);
            return 1;
        }
        log_info("real size: %ld", buf_size);
        map = (Elf64Hdr *)mmap(NULL, buf_size, PROT_READ, MAP_PRIVATE, fd, 0);
        log_info("--- ELF Header Information ---");
        log_info("Object file type : %d", map->e_type);
        log_info("Architecture : %d", map->e_machine);
        log_info("Object file version : %d", map->e_version);
        log_info("Entry point virtual address : 0x%08lx", map->e_entry);
        /**
         * [보안 메커니즘]
         * 1. PIE (Position Independent Executable): 바이너리가 특정 메모리 주소에 종속되지 않고 
         * 어느 위치(Offset)에서든 실행 가능하도록 컴파일된 상태를 의미함. (e_entry가 낮은 이유)
         * 2. ASLR (Address Space Layout Randomization): 커널이 프로세스 적재 시점에 코드(Code), 
         * 스택(Stack), 힙(Heap) 등의 주소를 무작위로 배치하여 메모리 오염 공격을 방어하는 기법.
         * -> 따라서 '실제 주소'는 'Base Address + e_entry(Offset)'의 형태로 결정됨.
         *
         * [주소 결정 메커니즘]
         * 1. 컴파일 타임: e_entry(0x2ba0)는 '건물 내부 설계도상 위치(Offset)'일 뿐임.
         * 2. 런타임(로더): 커널이 무작위로 '건물이 지어질 실제 땅(Base Address)'을 결정함.
         * 3. 보안 효과: 설계도(Offset)를 알아도 실제 땅(Base) 위치를 모르면 접근 불가!
         */
        extern int main(int, char**);
        log_info("Actual address of 'main': %p", (void*)main);
        if (map->e_entry != (unsigned long)main) {
            log_info("PIE is ACTIVE: File entry(0x%lx) != Memory address(0x%lx)", 
                    map->e_entry, (unsigned long)main);
            log_info("Relative Offset: 0x%lx", (unsigned long)main - map->e_entry);
        } else {
            log_info("PIE is INACTIVE: Static address used.");
        }
        log_info("Program header table file offset : %ld", map->e_phoff);
        munmap(map, buf_size);
    }

    return 1;
}

int toy_dump_state(char **args)
{
    (void)args;
    int mqretcode;
    mq_msg_t msg;

    msg.msg_type = DUMP_STATE;
    msg.param1 = 0;
    msg.param2 = 0;
    mqretcode = mq_send(monitor_queue, (char *)&msg, sizeof(msg), 0);
    assert(mqretcode == 0);

    return 1;
}

int toy_mincore(char **args)
{
    (void)args;
    unsigned char vec[20];
    int res;
    size_t page = sysconf(_SC_PAGESIZE);
    void *addr = mmap(NULL, 20 * page, PROT_READ | PROT_WRITE,
                      MAP_NORESERVE | MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
    res = mincore(addr, 10 * page, vec);
    assert(res == 0);

    return 1;
}

int toy_busy(char **args)
{
    (void)args;
    while (1)
        ;
    return 1;
}

int toy_shell(char **args)
{
    pid_t pid;
    int status;

    pid = fork();
    if (pid == 0) {
        if (execvp(args[0], args) == -1) {
            log_error("toy shell: %s", strerror(errno));
        }
        exit(EXIT_FAILURE);
    } else if (pid < 0) {
        log_error("toy shell: %s", strerror(errno));
    } else {
        do {
            waitpid(pid, &status, WUNTRACED);
        } while (!WIFEXITED(status) && !WIFSIGNALED(status));
    }

    return 1;
}

int toy_execute(char **args)
{
    int i;

    if (args[0] == NULL) {
        return 1;
    }

    for (i = 0; i < toy_num_builtins(); i++) {
        if (!strcmp(args[0], builtin_str[i])) {
            return (*builtin_func[i])(args);
        }
    }

    return 1;
}

char *toy_read_line(void)
{
    char *line = NULL;
    size_t bufsize = 0;

    if (getline(&line, &bufsize, stdin) == -1) {
        if (feof(stdin)) {
            exit(EXIT_SUCCESS);
        } else {
            log_error(": getline: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }
    }
    return line;
}

char **toy_split_line(char *line)
{
    int bufsize = TOY_TOK_BUFSIZE, position = 0;
    char **tokens = malloc(bufsize * sizeof(char *));
    char *token, **tokens_backup;

    if (!tokens) {
        fprintf(stderr, "toy: allocation error\n");
        exit(EXIT_FAILURE);
    }

    token = strtok(line, TOY_TOK_DELIM);
    while (token != NULL) {
        tokens[position] = token;
        position++;

        if (position >= bufsize) {
            bufsize += TOY_TOK_BUFSIZE;
            tokens_backup = tokens;
            tokens = realloc(tokens, bufsize * sizeof(char *));
            if (!tokens) {
                free(tokens_backup);
                fprintf(stderr, "toy: allocation error\n");
                exit(EXIT_FAILURE);
            }
        }

        token = strtok(NULL, TOY_TOK_DELIM);
    }
    tokens[position] = NULL;
    return tokens;
}

void toy_loop(void)
{
    char *line;
    char **args;
    int status;

    do {
        printf("linux-lab>");
        line = toy_read_line();
        args = toy_split_line(line);
        status = toy_execute(args);
        free(line);
        free(args);
    } while (status);
}

void *command_thread(void *arg)
{
    char *s = arg;

    log_debug("%s", s);

    usleep(2500000);
    toy_loop();

    return 0;
}

int input()
{
    /* 6.2.3. 시그널 */
    struct sigaction sa;
    /* 6.2.5. 스레드 */
    int retcode;
    pthread_t cTid;

    log_info("나 input 프로세스!");

    /* 6.2.3. 시그널 */
    memset(&sa, 0, sizeof(sigaction));
    sigemptyset(&sa.sa_mask);

    sa.sa_flags = SA_RESTART | SA_SIGINFO;
    sa.sa_sigaction = segfault_handler;

    if (sigaction(SIGSEGV, &sa, NULL) < 0) {
        log_error("sigaction failed!!");

        return 0;
    }

    /* 6.3.4. POSIX 메시지 큐 */
    /* 메시지 큐를 오픈 한다.
     * 하지만, 사실 fork로 생성했기 때문에 파일 디스크립터 공유되었음. 따라서, extern으로 사용 가능
     */
    watchdog_queue = mq_open("/watchdog_queue", O_RDWR);
    assert(watchdog_queue != -1);
    monitor_queue = mq_open("/monitor_queue", O_RDWR);
    assert(monitor_queue != -1);
    disk_queue = mq_open("/disk_queue", O_RDWR);
    assert(disk_queue != -1);

    /* 6.2.5. 스레드 */
    if ((retcode = pthread_create(&cTid, NULL, command_thread, "command thread") < 0)) {
        assert(retcode != 0);
        log_error("thread create error: %s", strerror(errno));
        exit(0);
    }

#ifdef PCP
    /* 생산자 소비자 실습 */
    int i;
    pthread_t thread[NUMTHREAD];

    pthread_mutex_lock(&global_message_mutex);
    strcpy(global_message, "hello world!");
    buflen = strlen(global_message);
    pthread_mutex_unlock(&global_message_mutex);
    pthread_create(&thread[0], NULL, (void *)toy_consumer, &thread_id[0]);
    pthread_create(&thread[1], NULL, (void *)toy_producer, &thread_id[1]);
    pthread_create(&thread[2], NULL, (void *)toy_producer, &thread_id[2]);

    for (i = 0; i < NUMTHREAD; i++) {
        pthread_join(thread[i], NULL);
    }
#endif /* PCP */

    while (1) {
        sleep(1);
    }

    return 0;
}

int create_input()
{
    /* 6.2.2. 프로세스 관련 시스템 콜 */
    pid_t systemPid;
    const char *name = "input";

    log_info("여기서 input 프로세스를 생성합니다.");

    systemPid = fork();
    if (!systemPid) {
        if (prctl(PR_SET_NAME, (unsigned long)name) < 0)
            log_error("prctl() error: %s", strerror(errno));
        input();
    } else if (systemPid > 0) {
        ;
    } else {
        log_error("fork() failed!!");
    }

    return 0;
}
