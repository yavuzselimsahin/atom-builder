#include "../include/exec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>

void pool_init(ProcPool *p) {
    p->count = 0;
    for (int i = 0; i < MAX_PROCS; i++) {
        p->procs[i].fd     = -1;
        p->procs[i].pid    = -1;
        p->procs[i].status = -1;
        p->procs[i].reaped = 0;
        p->procs[i].user   = NULL;
        sb_init(&p->procs[i].out);
    }
}

void pool_free(ProcPool *p) {
    for (int i = 0; i < p->count; i++) {
        if (p->procs[i].fd >= 0) close(p->procs[i].fd);
        sb_free(&p->procs[i].out);
    }
    p->count = 0;
}

/* Applies "NAME=VALUE" entries in the child, after fork, so the parent's own
   environment is never touched. */
static void apply_env(char *const env[]) {
    if (!env) return;

    for (int i = 0; env[i]; i++) {
        char *eq = strchr(env[i], '=');
        if (!eq) continue;

        size_t namelen = (size_t)(eq - env[i]);
        char   name[128];
        if (namelen >= sizeof name) continue;

        memcpy(name, env[i], namelen);
        name[namelen] = '\0';
        setenv(name, eq + 1, 1);
    }
}

Proc *pool_spawn(ProcPool *p, const char *label, const char *cwd,
                 char *const argv[], char *const env[]) {
    if (p->count >= MAX_PROCS) {
        fprintf(stderr, "atom: too many concurrent processes (max %d)\n",
                MAX_PROCS);
        return NULL;
    }

    int fds[2];
    if (pipe(fds) != 0) {
        fprintf(stderr, "atom: pipe: %s\n", strerror(errno));
        return NULL;
    }

    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "atom: fork: %s\n", strerror(errno));
        close(fds[0]);
        close(fds[1]);
        return NULL;
    }

    if (pid == 0) {
        /* Child. Nothing here may return: every failure path _exit()s, so a
           half-initialised child can never fall through and start behaving
           like the parent. */
        close(fds[0]);

        if (cwd && chdir(cwd) != 0) {
            fprintf(stderr, "chdir %s: %s\n", cwd, strerror(errno));
            _exit(126);
        }

        apply_env(env);

        if (dup2(fds[1], STDOUT_FILENO) < 0 ||
            dup2(fds[1], STDERR_FILENO) < 0) {
            _exit(126);
        }
        if (fds[1] > STDERR_FILENO) close(fds[1]);

        /* A build that tries to read stdin would otherwise hang the whole
           pool waiting for input nobody is going to type. */
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            if (devnull > STDERR_FILENO) close(devnull);
        }

        execvp(argv[0], argv);
        fprintf(stderr, "exec %s: %s\n", argv[0], strerror(errno));
        _exit(127);
    }

    /* Parent. */
    close(fds[1]);

    Proc *proc = &p->procs[p->count++];
    proc->pid     = pid;
    proc->fd      = fds[0];
    proc->status  = -1;
    proc->reaped  = 0;
    proc->started = now_seconds();
    proc->finished = 0;
    snprintf(proc->label, sizeof proc->label, "%s", label);
    sb_init(&proc->out);

    return proc;
}

/* Reaps `proc`, recording its exit status. A child killed by a signal is
   reported as 128+signal, matching shell convention. */
static void reap(Proc *proc) {
    if (proc->reaped) return;

    int raw = 0;
    while (waitpid(proc->pid, &raw, 0) < 0) {
        if (errno == EINTR) continue;
        proc->status = -1;
        proc->reaped = 1;
        return;
    }

    if (WIFEXITED(raw))        proc->status = WEXITSTATUS(raw);
    else if (WIFSIGNALED(raw)) proc->status = 128 + WTERMSIG(raw);
    else                       proc->status = -1;

    proc->reaped   = 1;
    proc->finished = now_seconds();
}

int pool_wait_all(ProcPool *p) {
    struct pollfd pfds[MAX_PROCS];
    int           idx[MAX_PROCS];

    for (;;) {
        int n = 0;
        for (int i = 0; i < p->count; i++) {
            if (p->procs[i].fd < 0) continue;
            pfds[n].fd      = p->procs[i].fd;
            pfds[n].events  = POLLIN;
            pfds[n].revents = 0;
            idx[n]          = i;
            n++;
        }
        if (n == 0) break;

        if (poll(pfds, (nfds_t)n, -1) < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "atom: poll: %s\n", strerror(errno));
            break;
        }

        for (int k = 0; k < n; k++) {
            if (!pfds[k].revents) continue;

            Proc *proc = &p->procs[idx[k]];
            char  buf[4096];

            ssize_t got = read(proc->fd, buf, sizeof buf);
            if (got > 0) {
                sb_append_n(&proc->out, buf, (size_t)got);
                continue;
            }
            if (got < 0 && errno == EINTR) continue;

            /* EOF, or an unrecoverable read error: the child is finished with
               this pipe either way. POLLHUP alone is not enough to stop
               reading, since buffered output can still be pending. */
            close(proc->fd);
            proc->fd = -1;
            reap(proc);
        }
    }

    /* Anything that never opened a pipe still needs collecting. */
    int failed = 0;
    for (int i = 0; i < p->count; i++) {
        reap(&p->procs[i]);
        if (p->procs[i].status != 0) failed++;
    }
    return failed;
}

int run_timed(const char *cwd, char *const argv[], StrBuf *out,
              int timeout_seconds) {
    ProcPool pool;
    pool_init(&pool);

    Proc *proc = pool_spawn(&pool, "timed", cwd, argv, NULL);
    if (!proc) {
        pool_free(&pool);
        return -1;
    }

    double deadline = now_seconds() + timeout_seconds;
    int    timed_out = 0;

    while (proc->fd >= 0) {
        double left = deadline - now_seconds();
        if (left <= 0) { timed_out = 1; break; }

        struct pollfd pfd = { proc->fd, POLLIN, 0 };
        int rc = poll(&pfd, 1, (int)(left * 1000) + 1);

        if (rc < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (rc == 0) { timed_out = 1; break; }

        char    buf[4096];
        ssize_t got = read(proc->fd, buf, sizeof buf);
        if (got > 0) {
            sb_append_n(&proc->out, buf, (size_t)got);
        } else if (got < 0 && errno == EINTR) {
            continue;
        } else {
            close(proc->fd);
            proc->fd = -1;
        }
    }

    if (timed_out) {
        kill(proc->pid, SIGKILL);
        if (proc->fd >= 0) { close(proc->fd); proc->fd = -1; }
    }

    reap(proc);

    if (out && proc->out.data)
        sb_append_n(out, proc->out.data, proc->out.len);

    pool_free(&pool);
    return timed_out ? RUN_TIMEOUT : proc->status;
}

int run_with_input(const char *cwd, char *const argv[],
                   const char *input, size_t input_len, StrBuf *out) {
    int in_fds[2], out_fds[2];

    if (pipe(in_fds) != 0) return -1;
    if (pipe(out_fds) != 0) {
        close(in_fds[0]);
        close(in_fds[1]);
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(in_fds[0]);  close(in_fds[1]);
        close(out_fds[0]); close(out_fds[1]);
        return -1;
    }

    if (pid == 0) {
        close(in_fds[1]);
        close(out_fds[0]);

        if (cwd && chdir(cwd) != 0) _exit(126);
        if (dup2(in_fds[0], STDIN_FILENO) < 0)   _exit(126);
        if (dup2(out_fds[1], STDOUT_FILENO) < 0) _exit(126);

        if (in_fds[0]  > STDERR_FILENO) close(in_fds[0]);
        if (out_fds[1] > STDERR_FILENO) close(out_fds[1]);

        execvp(argv[0], argv);
        _exit(127);
    }

    close(in_fds[0]);
    close(out_fds[1]);

    size_t written = 0;
    int    writing = 1;

    while (writing || out_fds[0] >= 0) {
        struct pollfd pfds[2];
        int n = 0;

        if (writing) {
            pfds[n].fd      = in_fds[1];
            pfds[n].events  = POLLOUT;
            pfds[n].revents = 0;
            n++;
        }
        if (out_fds[0] >= 0) {
            pfds[n].fd      = out_fds[0];
            pfds[n].events  = POLLIN;
            pfds[n].revents = 0;
            n++;
        }
        if (n == 0) break;

        if (poll(pfds, (nfds_t)n, -1) < 0) {
            if (errno == EINTR) continue;
            break;
        }

        for (int k = 0; k < n; k++) {
            if (!pfds[k].revents) continue;

            if (writing && pfds[k].fd == in_fds[1]) {
                ssize_t put = write(in_fds[1], input + written,
                                    input_len - written);
                if (put > 0) written += (size_t)put;
                else if (put < 0 && errno == EINTR) continue;
                else { written = input_len; }   /* child closed its stdin */

                if (written >= input_len) {
                    close(in_fds[1]);
                    writing = 0;
                }
            } else if (pfds[k].fd == out_fds[0]) {
                char    buf[4096];
                ssize_t got = read(out_fds[0], buf, sizeof buf);
                if (got > 0) {
                    if (out) sb_append_n(out, buf, (size_t)got);
                } else if (got < 0 && errno == EINTR) {
                    continue;
                } else {
                    close(out_fds[0]);
                    out_fds[0] = -1;
                }
            }
        }
    }

    if (writing) close(in_fds[1]);
    if (out_fds[0] >= 0) close(out_fds[0]);

    int raw = 0;
    while (waitpid(pid, &raw, 0) < 0) {
        if (errno != EINTR) return -1;
    }

    if (WIFEXITED(raw))   return WEXITSTATUS(raw);
    if (WIFSIGNALED(raw)) return 128 + WTERMSIG(raw);
    return -1;
}

int run_capture(const char *cwd, char *const argv[], StrBuf *out) {
    int fds[2];
    if (pipe(fds) != 0) return -1;

    pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return -1;
    }

    if (pid == 0) {
        close(fds[0]);
        if (cwd && chdir(cwd) != 0) _exit(126);
        if (dup2(fds[1], STDOUT_FILENO) < 0) _exit(126);
        if (fds[1] > STDERR_FILENO) close(fds[1]);

        int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            if (devnull > STDERR_FILENO) close(devnull);
        }

        execvp(argv[0], argv);
        _exit(127);
    }

    close(fds[1]);

    char    buf[8192];
    ssize_t got;
    while ((got = read(fds[0], buf, sizeof buf)) != 0) {
        if (got < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (out && sb_append_n(out, buf, (size_t)got) != 0) break;
    }
    close(fds[0]);

    int raw = 0;
    while (waitpid(pid, &raw, 0) < 0) {
        if (errno != EINTR) return -1;
    }

    if (WIFEXITED(raw))        return WEXITSTATUS(raw);
    if (WIFSIGNALED(raw))      return 128 + WTERMSIG(raw);
    return -1;
}

int run_sync(const char *cwd, char *const argv[], StrBuf *out) {
    ProcPool pool;
    pool_init(&pool);

    Proc *proc = pool_spawn(&pool, "sync", cwd, argv, NULL);
    if (!proc) {
        pool_free(&pool);
        return -1;
    }

    pool_wait_all(&pool);
    int status = proc->status;

    if (out && proc->out.data)
        sb_append_n(out, proc->out.data, proc->out.len);

    pool_free(&pool);
    return status;
}
