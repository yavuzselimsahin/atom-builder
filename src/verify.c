#include "../include/verify.h"
#include "../include/binfmt.h"
#include "../include/exec.h"
#include "../include/driver.h"
#include "../include/util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/utsname.h>
#include <sys/stat.h>

const char *runner_name(RunnerKind kind) {
    switch (kind) {
        case RUN_HOST:   return "host";
        case RUN_QEMU:   return "qemu";
        case RUN_DOCKER: return "docker";
        case RUN_WINE:   return "wine";
        case RUN_WINE_DOCKER: return "docker+wine";
        default:         return "none";
    }
}

/* What this machine is, as the two facts that decide whether a binary runs. */
static void host_identity(BinFormat *fmt, BinArch *arch) {
    struct utsname u;

    *fmt  = BF_UNKNOWN;
    *arch = BA_UNKNOWN;

    if (uname(&u) != 0) return;

    if      (strcmp(u.sysname, "Darwin") == 0) *fmt = BF_MACHO;
    else if (strcmp(u.sysname, "Linux")  == 0) *fmt = BF_ELF;
    else                                       *fmt = BF_ELF;  /* the BSDs */

    if      (strcmp(u.machine, "x86_64")  == 0) *arch = BA_X86_64;
    else if (strcmp(u.machine, "amd64")   == 0) *arch = BA_X86_64;
    else if (strcmp(u.machine, "arm64")   == 0) *arch = BA_AARCH64;
    else if (strcmp(u.machine, "aarch64") == 0) *arch = BA_AARCH64;
}

static int have_command(const char *name) {
    ArgV a;
    av_init(&a);
    av_push(&a, "which");
    av_push(&a, name);

    StrBuf out;
    sb_init(&out);
    int rc = run_sync(NULL, a.v, &out);

    av_free(&a);
    sb_free(&out);
    return rc == 0;
}

static int docker_ready(void) {
    ArgV a;
    av_init(&a);
    av_push(&a, "docker");
    av_push(&a, "info");

    StrBuf out;
    sb_init(&out);
    int rc = run_timed(NULL, a.v, &out, 20);

    av_free(&a);
    sb_free(&out);
    return rc == 0;
}

static const char *qemu_binary(BinArch arch) {
    switch (arch) {
        case BA_X86_64:  return "qemu-x86_64";
        case BA_AARCH64: return "qemu-aarch64";
        case BA_ARM:     return "qemu-arm";
        case BA_I386:    return "qemu-i386";
        case BA_RISCV64: return "qemu-riscv64";
        case BA_PPC64:   return "qemu-ppc64";
        default:         return NULL;
    }
}

#define docker_platform binfmt_docker_platform

RunnerKind runner_select(const Manifest *m, const char *triple,
                         char *why, size_t why_size) {

    BinFormat want_fmt;
    BinArch   want_arch;
    if (binfmt_from_triple(triple, &want_fmt, &want_arch) != 0) {
        snprintf(why, why_size, "unrecognised triple");
        return RUN_NONE;
    }

    BinFormat host_fmt;
    BinArch   host_arch;
    host_identity(&host_fmt, &host_arch);

    /* Same OS and same architecture: just run it. macOS additionally runs
       x86_64 on Apple silicon through Rosetta, so that counts as native. */
    if (want_fmt == host_fmt) {
        if (want_arch == host_arch) return RUN_HOST;

        if (host_fmt == BF_MACHO && host_arch == BA_AARCH64 &&
            want_arch == BA_X86_64)
            return RUN_HOST;
    }

    if (want_fmt == BF_PE) {
        if (have_command("wine")) return RUN_WINE;

        /* The same fallback Linux targets get, but only when asked for: a
           wine image is gigabytes, where the Linux ones are megabytes, so it
           is named in the manifest rather than assumed. */
        if (m->wine_image[0] && docker_platform(want_arch) && docker_ready())
            return RUN_WINE_DOCKER;

        snprintf(why, why_size, m->wine_image[0]
                 ? "no wine here, and %s could not be used"
                 : "no wine on this host (set [verify] wine_image to use one "
                   "in a container)", m->wine_image);
        return RUN_NONE;
    }

    if (want_fmt == BF_ELF) {
        /* qemu-user translates Linux syscalls, so it needs a Linux host. On
           macOS the equivalent is a Linux container, which brings its own. */
        if (host_fmt == BF_ELF) {
            const char *q = qemu_binary(want_arch);
            if (q && have_command(q)) return RUN_QEMU;
        }

        if (docker_platform(want_arch)) {
            if (docker_ready()) return RUN_DOCKER;
            snprintf(why, why_size, "needs qemu-user or a running docker");
            return RUN_NONE;
        }
    }

    if (want_fmt == BF_MACHO) {
        snprintf(why, why_size, "macOS binaries only run on macOS");
        return RUN_NONE;
    }

    snprintf(why, why_size, "no runner for %s", triple);
    return RUN_NONE;
}

/* Builds the command that executes `binary` under the chosen runner. */
static int runner_argv(RunnerKind kind, const Manifest *m, const Target *t,
                       const char *binary, const char *dist_abs,
                       const char *artifact, ArgV *a) {
    av_init(a);

    switch (kind) {
        case RUN_HOST:
            av_push(a, binary);
            break;

        case RUN_QEMU: {
            BinFormat f;
            BinArch   arch;
            binfmt_from_triple(t->triple, &f, &arch);
            const char *q = qemu_binary(arch);
            if (!q) { av_free(a); return -1; }
            av_push(a, q);
            av_push(a, binary);
            break;
        }

        case RUN_WINE:
            av_push(a, "wine");
            av_push(a, binary);
            break;

        case RUN_WINE_DOCKER: {
            image_note(m->wine_image);
            BinFormat f;
            BinArch   arch;
            binfmt_from_triple(t->triple, &f, &arch);
            const char *plat = docker_platform(arch);
            if (!plat) { av_free(a); return -1; }

            av_push(a, "docker");
            av_push(a, "run");
            av_push(a, "--rm");
            av_push(a, "--platform");
            av_push(a, plat);
            av_push(a, "-v");
            av_pushf(a, "%s:/atom:ro", dist_abs);
            /* wine narrates its own startup at length; none of it is the
               program's output, and expect would have to match around it. */
            av_push(a, "-e");
            av_push(a, "WINEDEBUG=-all");
            av_push(a, m->wine_image);
            av_push(a, "wine");
            av_pushf(a, "/atom/%s/%s", t->id, artifact);
            break;
        }

        case RUN_DOCKER: {
            image_note(m->verify_image);
            BinFormat f;
            BinArch   arch;
            binfmt_from_triple(t->triple, &f, &arch);
            const char *plat = docker_platform(arch);
            if (!plat) { av_free(a); return -1; }

            /* dist/ is mounted read-only, so a misbehaving binary under
               emulation cannot alter what is about to be released. */
            av_push(a, "docker");
            av_push(a, "run");
            av_push(a, "--rm");
            av_push(a, "--platform");
            av_push(a, plat);
            av_push(a, "-v");
            av_pushf(a, "%s:/atom:ro", dist_abs);
            av_push(a, m->verify_image);
            av_pushf(a, "/atom/%s/%s", t->id, artifact);
            break;
        }

        default:
            av_free(a);
            return -1;
    }

    if (av_push_split(a, m->verify_args) != 0) {
        av_free(a);
        return -1;
    }
    return 0;
}

typedef struct {
    const Target *target;
    RunnerKind    kind;
    int           ok;
    int           skipped;
    double        seconds;
    char          detail[220];
} VerifyJob;

int cmd_verify(const Manifest *m, const BuildOpts *o) {
    char dist_abs[PATH_MAX];
    if (!realpath(o->dist_dir, dist_abs)) {
        fprintf(stderr, "atom: nothing built yet — run `atom build` first\n");
        return 1;
    }

    VerifyJob jobs[MAX_TARGETS];
    memset(jobs, 0, sizeof jobs);

    int selected = 0;
    for (int i = 0; i < m->target_count; i++) {
        if (o->only && strcmp(o->only, m->targets[i].id) != 0) continue;
        jobs[selected++].target = &m->targets[i];
    }
    if (selected == 0) {
        fprintf(stderr, "atom: no target named '%s'\n", o->only);
        return 1;
    }

    printf("%s %s — verifying %d target%s\n", m->name, m->version,
           selected, selected == 1 ? "" : "s");

    double t0 = now_seconds();

    for (int i = 0; i < selected; i++) {
        VerifyJob    *job = &jobs[i];
        const Target *t   = job->target;
        const char   *art = target_artifact(m, t);

        char binary[1024];
        snprintf(binary, sizeof binary, "%s/%s/%s", dist_abs, t->id, art);

        struct stat st;
        if (stat(binary, &st) != 0) {
            snprintf(job->detail, sizeof job->detail,
                     "not built — run `atom build` first");
            continue;
        }

        char why[160] = "";
        job->kind = runner_select(m, t->triple, why, sizeof why);

        if (job->kind == RUN_NONE) {
            job->skipped = 1;
            snprintf(job->detail, sizeof job->detail, "%s", why);
            continue;
        }

        ArgV a;
        if (runner_argv(job->kind, m, t, binary, dist_abs, art, &a) != 0) {
            snprintf(job->detail, sizeof job->detail,
                     "could not build the %s command", runner_name(job->kind));
            continue;
        }

        StrBuf out;
        sb_init(&out);

        double started = now_seconds();
        int    rc      = run_timed(NULL, a.v, &out, m->verify_timeout);
        job->seconds   = now_seconds() - started;

        av_free(&a);

        if (rc == RUN_TIMEOUT) {
            snprintf(job->detail, sizeof job->detail,
                     "did not finish within %ds", m->verify_timeout);
        } else if (rc != m->verify_exit) {
            snprintf(job->detail, sizeof job->detail,
                     "exited %d, expected %d", rc, m->verify_exit);
        } else if (m->verify_expect[0] &&
                   (!out.data || !strstr(out.data, m->verify_expect))) {
            /* atomik-ssg exits 0 even for an unknown command, which is exactly
               why a status code alone is not enough of a signal. */
            snprintf(job->detail, sizeof job->detail,
                     "output did not contain \"%s\"", m->verify_expect);
        } else {
            job->ok = 1;
            snprintf(job->detail, sizeof job->detail, "ran under %s",
                     runner_name(job->kind));
        }

        if (!job->ok && o->verbose && out.data)
            printf("\n--- %s ---\n%s\n", t->id, out.data);

        sb_free(&out);
    }

    int failed = 0, skipped = 0;
    putchar('\n');

    for (int i = 0; i < selected; i++) {
        VerifyJob *job = &jobs[i];

        if (job->ok) {
            printf("  ok    %-18s %6.2fs  %s\n",
                   job->target->id, job->seconds, job->detail);
        } else if (job->skipped && !job->target->verify) {
            skipped++;
            printf("  skip  %-18s          %s\n",
                   job->target->id, job->detail);
        } else {
            /* A target that asked to be verified cannot quietly go unchecked;
               that would defeat the point of asking. */
            failed++;
            printf("  FAIL  %-18s %6.2fs  %s\n",
                   job->target->id, job->seconds, job->detail);
        }
    }

    printf("\n%d/%d verified", selected - failed - skipped, selected);
    if (skipped) printf(", %d skipped", skipped);
    printf(" in %.2fs\n", now_seconds() - t0);

    if (skipped && !o->verbose)
        printf("\nSkipped targets have no runner on this host. `atom verify -v`"
               " explains each one.\n");

    image_cleanup(o->keep_images);

    return failed;
}
