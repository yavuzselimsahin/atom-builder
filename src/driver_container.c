#include "../include/driver.h"
#include "../include/binfmt.h"
#include "../include/exec.h"

#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>

/* The container is kept alive by a long sleep and driven with `docker exec`,
   rather than by a single `docker run` per step. That is what lets `setup` and
   the build share one filesystem: packages installed by the first are still
   there for the second. */
#define CONTAINER_LIFETIME "7200"

int container_start(const Target *t, const char *workdir,
                    char *id_out, size_t id_size) {
    id_out[0] = '\0';

    BinFormat fmt;
    BinArch   arch;
    if (binfmt_from_triple(t->triple, &fmt, &arch) != 0) {
        fprintf(stderr, "atom: %s: unrecognised triple %s\n",
                t->id, t->triple);
        return -1;
    }

    const char *platform = binfmt_docker_platform(arch);
    if (!platform) {
        fprintf(stderr, "atom: %s: docker has no platform for %s\n",
                t->id, binfmt_arch_name(arch));
        return -1;
    }

    /* Docker needs an absolute path for a bind mount. */
    char abs[PATH_MAX];
    if (!realpath(workdir, abs)) {
        fprintf(stderr, "atom: %s: cannot resolve %s\n", t->id, workdir);
        return -1;
    }

    image_note(t->image);

    ArgV a;
    av_init(&a);
    av_push(&a, "docker");
    av_push(&a, "run");
    av_push(&a, "-d");
    av_push(&a, "--rm");
    av_push(&a, "--platform");
    av_push(&a, platform);
    av_push(&a, "-v");
    av_pushf(&a, "%s:/src", abs);
    av_push(&a, "-w");
    av_push(&a, "/src");
    av_push(&a, t->image);
    av_push(&a, "sleep");
    av_push(&a, CONTAINER_LIFETIME);

    StrBuf out;
    sb_init(&out);
    int rc = run_capture(NULL, a.v, &out);
    av_free(&a);

    if (rc != 0 || !out.data) {
        fprintf(stderr, "atom: %s: could not start %s\n", t->id, t->image);
        sb_free(&out);
        return -1;
    }

    snprintf(id_out, id_size, "%s", out.data);
    id_out[strcspn(id_out, "\r\n")] = '\0';
    sb_free(&out);

    if (!id_out[0]) {
        fprintf(stderr, "atom: %s: docker returned no container id\n", t->id);
        return -1;
    }
    return 0;
}

int container_setup(const Target *t, const char *id, StrBuf *log) {
    if (!t->setup[0]) return 0;

    ArgV a;
    av_init(&a);
    av_push(&a, "docker");
    av_push(&a, "exec");
    av_push(&a, id);
    av_push(&a, "sh");
    av_push(&a, "-c");
    av_push(&a, t->setup);      /* one argv element: the shell is the guest's */

    int rc = run_sync(NULL, a.v, log);
    av_free(&a);

    return rc == 0 ? 0 : -1;
}

int container_exec_argv(const char *id, char *const command[], ArgV *out) {
    av_init(out);
    av_push(out, "docker");
    av_push(out, "exec");
    av_push(out, id);

    for (int i = 0; command[i]; i++) {
        if (av_push(out, command[i]) != 0) {
            av_free(out);
            return -1;
        }
    }
    return 0;
}

void container_stop(const char *id) {
    if (!id || !id[0]) return;

    ArgV a;
    av_init(&a);
    av_push(&a, "docker");
    av_push(&a, "rm");
    av_push(&a, "-f");
    av_push(&a, id);

    StrBuf out;
    sb_init(&out);
    run_sync(NULL, a.v, &out);

    av_free(&a);
    sb_free(&out);
}

/* --------------------------------------------------------------------- */
/* Images atom introduced, and taking them back out                        */
/* --------------------------------------------------------------------- */

#define MAX_NOTED 16

static char noted[MAX_NOTED][128];
static int  noted_count;

static int image_present(const char *image) {
    ArgV a;
    av_init(&a);
    av_push(&a, "docker");
    av_push(&a, "image");
    av_push(&a, "inspect");
    av_push(&a, image);

    StrBuf out;
    sb_init(&out);
    int rc = run_sync(NULL, a.v, &out);

    av_free(&a);
    sb_free(&out);
    return rc == 0;
}

void image_note(const char *image) {
    if (!image || !image[0] || noted_count >= MAX_NOTED) return;

    for (int i = 0; i < noted_count; i++)
        if (strcmp(noted[i], image) == 0) return;

    /* Only images this machine did not already have. Checking before the run
       is the only moment the answer is knowable. */
    if (image_present(image)) return;

    snprintf(noted[noted_count], sizeof noted[0], "%s", image);
    noted_count++;
}

void image_cleanup(int keep) {
    if (noted_count == 0) return;

    if (keep) {
        printf("\nkept %d pulled image%s:\n", noted_count,
               noted_count == 1 ? "" : "s");
        for (int i = 0; i < noted_count; i++)
            printf("  %s\n", noted[i]);
        noted_count = 0;
        return;
    }

    printf("\nremoving %d image%s atom pulled:\n", noted_count,
           noted_count == 1 ? "" : "s");

    for (int i = 0; i < noted_count; i++) {
        ArgV a;
        av_init(&a);
        av_push(&a, "docker");
        av_push(&a, "image");
        av_push(&a, "rm");
        av_push(&a, noted[i]);

        StrBuf out;
        sb_init(&out);
        int rc = run_sync(NULL, a.v, &out);
        av_free(&a);
        sb_free(&out);

        /* A removal can fail because something else started using the image
           meanwhile. That is not atom's business to force. */
        printf("  %-40s %s\n", noted[i], rc == 0 ? "removed" : "still in use");
    }
    noted_count = 0;
}
