#ifndef EXEC_H
#define EXEC_H

#include <sys/types.h>
#include "util.h"

#define MAX_PROCS 64

/* One child process, its merged stdout+stderr, and its result. */
typedef struct {
    pid_t  pid;
    int    fd;         /* read end of the merged output pipe, -1 once closed */
    char   label[64];  /* shown in progress and failure output              */
    StrBuf out;        /* everything the child wrote                        */
    int    status;     /* exit code once reaped, -1 while running           */
    int    reaped;
    double started;
    double finished;
    void  *user;       /* opaque, for the caller to correlate results       */
} Proc;

typedef struct {
    Proc procs[MAX_PROCS];
    int  count;
} ProcPool;

void pool_init(ProcPool *p);
void pool_free(ProcPool *p);

/* Forks a child running argv[0] with argv, inside `cwd` (may be NULL for the
   current directory). stdout and stderr are merged into one pipe and captured.

   `env` is a NULL-terminated array of "NAME=VALUE" strings applied on top of
   the inherited environment, or NULL. Adding rather than replacing matters:
   a build still needs PATH and HOME, and some toolchains select the target
   through the environment rather than through flags.

   Returns the Proc on success, NULL on failure. */
Proc *pool_spawn(ProcPool *p, const char *label, const char *cwd,
                 char *const argv[], char *const env[]);

/* Drives every live child to completion, multiplexing their output with
   poll(). Returns the number that failed. */
int pool_wait_all(ProcPool *p);

/* Runs one command to completion and captures its output. Returns the exit
   code, or -1 if the process could not be started. `out` may be NULL. */
int run_sync(const char *cwd, char *const argv[], StrBuf *out);

/* Like run_sync, but captures stdout alone and lets stderr through to the
   parent. Required whenever the output is binary: merging the two streams
   would splice any warning the child prints into the middle of the data. */
int run_capture(const char *cwd, char *const argv[], StrBuf *out);

/* Feeds `input` to the child's stdin while capturing its stdout. This is how
   secrets reach a command without ever appearing in argv, where any user on
   the machine could read them out of `ps`. Both directions are multiplexed, so
   a child that talks back before stdin is closed cannot deadlock. */
int run_with_input(const char *cwd, char *const argv[],
                   const char *input, size_t input_len, StrBuf *out);

/* Exit status reported when a command outlives its deadline. */
#define RUN_TIMEOUT (-2)

/* Runs a command with merged output and a deadline, killing it if it overruns.
   Anything executing a freshly built binary needs this: a program that hangs
   under emulation would otherwise hang atom with it. */
int run_timed(const char *cwd, char *const argv[], StrBuf *out,
              int timeout_seconds);

#endif
