#include "../include/build.h"
#include "../include/exec.h"
#include "../include/binfmt.h"
#include "../include/cache.h"
#include "../include/driver.h"
#include "../include/buildsys.h"
#include "../include/util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>

typedef struct {
    const Target *target;
    char  work[1024];      /* isolated tree for this target        */
    char  produced[1024];  /* artifact inside the work tree        */
    char  shipped[1024];   /* artifact copied into dist/           */
    ArgV  argv;
    int   prepared;
    int   ok;
    char  detail[160];     /* what went wrong, or what was produced */
    long long size;
    double seconds;
    char  key[SHA256_HEX_LEN];
    int   has_key;
    int   cached;          /* satisfied without running the build   */
    char  container[CONTAINER_ID_LEN];
    ArgV  env;             /* NAME=VALUE entries for the build              */
    ArgV  spawn;           /* what is actually run: argv, or docker exec argv */
} Job;

/* Directories that must never be cloned into a target's work tree: the work
   and dist trees themselves (copying them would recurse), and version control
   metadata, which no build needs. */
static int is_excluded(const char *name, const BuildOpts *o) {
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return 1;
    if (strcmp(name, ".git") == 0) return 1;

    const char *w = strrchr(o->work_dir, '/');
    const char *d = strrchr(o->dist_dir, '/');
    w = w ? w + 1 : o->work_dir;
    d = d ? d + 1 : o->dist_dir;

    return strcmp(name, w) == 0 || strcmp(name, d) == 0;
}

/* Clones the source tree into dest. `cp -Rc` uses APFS clonefile, which is
   constant-time and shares storage; the plain -R retry covers every other
   filesystem. Entries are passed in one invocation rather than copying the
   directory itself, so exclusions can be honoured. */
static int clone_tree(const char *src, const char *dest, const BuildOpts *o) {
    if (make_dir(dest) != 0) {
        fprintf(stderr, "atom: mkdir %s: %s\n", dest, strerror(errno));
        return -1;
    }

    DIR *dir = opendir(src);
    if (!dir) {
        fprintf(stderr, "atom: opendir %s: %s\n", src, strerror(errno));
        return -1;
    }

    ArgV a;
    av_init(&a);
    av_push(&a, "cp");
    av_push(&a, "-Rc");

    int entries = 0;
    struct dirent *e;
    while ((e = readdir(dir))) {
        if (is_excluded(e->d_name, o)) continue;
        if (av_pushf(&a, "%s/%s", src, e->d_name) != 0) break;
        entries++;
    }
    closedir(dir);

    if (entries == 0) {
        fprintf(stderr, "atom: %s has nothing to build\n", src);
        av_free(&a);
        return -1;
    }

    av_push(&a, dest);

    int rc = run_sync(NULL, a.v, NULL);
    if (rc != 0) {
        free(a.v[1]);
        a.v[1] = strdup("-R");           /* no clonefile here; copy for real */
        rc = a.v[1] ? run_sync(NULL, a.v, NULL) : -1;
    }

    av_free(&a);
    return rc == 0 ? 0 : -1;
}

/* The source tree may carry object files from an earlier native build. Left in
   place they link into the wrong architecture and the build still exits 0, so
   the project's own clean rule runs before every target. */
static void clean_tree(const Manifest *m, const BuildVars *v, const char *work,
                       char *const env[]) {
    const char *tmpl = manifest_template(m, "clean");
    if (!tmpl[0]) return;

    char expanded[1024];
    if (buildsys_expand(tmpl, v, expanded, sizeof expanded) != 0) return;

    ArgV a;
    av_init(&a);
    if (av_push_split(&a, expanded) == 0 && a.count > 0) {
        ProcPool pool;
        pool_init(&pool);
        if (pool_spawn(&pool, "clean", work, a.v, env)) pool_wait_all(&pool);
        pool_free(&pool);
    }
    av_free(&a);
}

/* Assembles what will actually run, from the build system's templates. */
static int build_commands(const Manifest *m, const Target *t,
                          const BuildVars *v, ArgV *argv, ArgV *env) {
    const BuildSystem *sys = buildsys_find(m->system);
    if (!sys) return -1;

    av_init(argv);
    av_init(env);

    char expanded[2048];

    if (buildsys_expand(manifest_template(m, "command"), v,
                        expanded, sizeof expanded) != 0) goto fail;
    if (av_push_split(argv, expanded) != 0 || argv->count == 0) goto fail;

    /* The container and native strategies compile with whatever `cc` the
       environment provides — under container, that environment *is* the
       target, which is the entire reason to pay for it. Only the zig strategy
       cross-compiles, and only make-shaped systems take the compiler as a
       variable assignment, in one argv entry however many spaces it holds. */
    if (sys->cc_var[0] && strcmp(t->strategy, "zig") == 0) {
        if (buildsys_expand(sys->cc_var, v, expanded, sizeof expanded) != 0)
            goto fail;
        if (av_push(argv, expanded) != 0) goto fail;
    }

    if (m->strip && sys->strip_flag[0]) {
        if (buildsys_expand(sys->strip_flag, v, expanded, sizeof expanded) != 0)
            goto fail;
        if (av_push_split(argv, expanded) != 0) goto fail;
    }

    /* Last, so a target can override anything set above. */
    if (t->make_args[0] && av_push_split(argv, t->make_args) != 0) goto fail;

    if (sys->env[0]) {
        if (buildsys_expand(sys->env, v, expanded, sizeof expanded) != 0)
            goto fail;
        if (av_push_split(env, expanded) != 0) goto fail;
    }

    return 0;

fail:
    av_free(argv);
    av_free(env);
    return -1;
}

/* Copies one file, preserving the executable bit. */
static int copy_file(const char *src, const char *dst) {
    ArgV a;
    av_init(&a);
    av_push(&a, "cp");
    av_push(&a, "-p");
    av_push(&a, src);
    av_push(&a, dst);

    int rc = run_sync(NULL, a.v, NULL);
    av_free(&a);
    return rc == 0 ? 0 : -1;
}

/* Confirms the artifact is the thing the target asked for. A build that
   silently linked stale objects exits 0 and leaves a plausible-looking file
   behind; the header is the only witness. */
static int verify_artifact(Job *job, const Target *t, const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        snprintf(job->detail, sizeof job->detail,
                 "build reported success but %s was never created", path);
        return -1;
    }
    job->size = (long long)st.st_size;

    BinInfo got;
    if (binfmt_probe(path, &got) != 0) {
        snprintf(job->detail, sizeof job->detail,
                 "%s is not a recognisable executable", path);
        return -1;
    }

    BinFormat want_fmt;
    BinArch   want_arch;
    if (binfmt_from_triple(t->triple, &want_fmt, &want_arch) != 0) {
        /* An unfamiliar triple is not grounds for rejecting the build; report
           what was produced and let the user judge. */
        binfmt_describe(&got, job->detail, sizeof job->detail);
        return 0;
    }

    if (got.fmt != want_fmt || got.arch != want_arch) {
        char actual[80];
        binfmt_describe(&got, actual, sizeof actual);
        snprintf(job->detail, sizeof job->detail,
                 "wrong artifact: expected %s %s, got %s",
                 binfmt_format_name(want_fmt), binfmt_arch_name(want_arch),
                 actual);
        return -1;
    }

    binfmt_describe(&got, job->detail, sizeof job->detail);
    return 0;
}

int cmd_build(const Manifest *m, const BuildOpts *o) {
    Job jobs[MAX_TARGETS];
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

    if (make_dir(o->work_dir) != 0 || make_dir(o->dist_dir) != 0) {
        fprintf(stderr, "atom: cannot create output directories: %s\n",
                strerror(errno));
        return 1;
    }

    printf("%s %s — %d target%s\n", m->name, m->version,
           selected, selected == 1 ? "" : "s");

    /* Phase 1: isolate. Sequential and cheap; every target gets a private,
       verified-clean tree before any compiler runs. */
    double t0 = now_seconds();
    for (int i = 0; i < selected; i++) {
        Job *job = &jobs[i];
        const Target *t = job->target;
        const char *art = target_artifact(m, t);

        snprintf(job->work, sizeof job->work, "%s/%s", o->work_dir, t->id);

        BuildVars vars;
        if (buildvars_init(&vars, t->triple, art, m->name, m->version,
                           o->make_jobs > 0 ? o->make_jobs : 4) != 0) {
            snprintf(job->detail, sizeof job->detail,
                     "unrecognised triple %s", t->triple);
            continue;
        }

        if (build_commands(m, t, &vars, &job->argv, &job->env) != 0) {
            snprintf(job->detail, sizeof job->detail,
                     "could not assemble the build command");
            continue;
        }

        char dest_dir[1024];
        snprintf(dest_dir, sizeof dest_dir, "%s/%s", o->dist_dir, t->id);

        /* The key is computed before anything is cloned, so a hit costs one
           pass over the sources and nothing else. */
        if (!o->no_cache &&
            cache_key(m, t, o, job->argv.v, job->env.v, toolchain_id(), job->key) == 0) {
            job->has_key = 1;

            if (make_dir(dest_dir) == 0) {
                snprintf(job->shipped, sizeof job->shipped, "%s/%s",
                         dest_dir, art);

                if (cache_lookup(o, t, job->key, art, job->shipped) == 0 &&
                    verify_artifact(job, t, job->shipped) == 0) {
                    job->ok     = 1;
                    job->cached = 1;
                    continue;
                }
            }
        }

        if (remove_tree(job->work) != 0) {
            snprintf(job->detail, sizeof job->detail,
                     "could not clear %s", job->work);
            continue;
        }
        if (clone_tree(o->source_root, job->work, o) != 0) {
            snprintf(job->detail, sizeof job->detail,
                     "could not prepare %s", job->work);
            continue;
        }

        clean_tree(m, &vars, job->work, job->env.v);

        /* Where the artifact lands is the build system's business: make leaves
           it at the root, cargo under target/<triple>/release, dotnet in the
           publish directory. */
        char rel[1024];
        if (buildsys_expand(manifest_template(m, "output"), &vars,
                            rel, sizeof rel) != 0) {
            snprintf(job->detail, sizeof job->detail,
                     "could not resolve where the artifact lands");
            continue;
        }
        snprintf(job->produced, sizeof job->produced, "%s/%s", job->work, rel);

        /* A container target needs its environment standing before the build
           can be spawned into it. Both steps are sequential on purpose: a
           `setup` that installs a toolchain is slow, and running several at
           once would compete for the same network and package mirrors. */
        if (strcmp(t->strategy, "container") == 0) {
            if (container_start(t, job->work, job->container,
                                sizeof job->container) != 0) {
                snprintf(job->detail, sizeof job->detail,
                         "could not start %s", t->image);
                continue;
            }

            StrBuf log;
            sb_init(&log);
            if (container_setup(t, job->container, &log) != 0) {
                snprintf(job->detail, sizeof job->detail,
                         "setup failed inside %s", t->image);
                if (o->verbose && log.data)
                    printf("\n--- %s setup ---\n%s\n", t->id, log.data);
                sb_free(&log);
                container_stop(job->container);
                job->container[0] = '\0';
                continue;
            }
            sb_free(&log);

            if (container_exec_argv(job->container, job->argv.v,
                                    &job->spawn) != 0) {
                snprintf(job->detail, sizeof job->detail,
                         "could not assemble the container command");
                container_stop(job->container);
                job->container[0] = '\0';
                continue;
            }
        } else {
            /* The command runs directly, in the target's own work tree. */
            av_init(&job->spawn);
            for (int k = 0; k < job->argv.count; k++)
                av_push(&job->spawn, job->argv.v[k]);
        }

        job->prepared = 1;
    }

    /* Phase 2: build, all targets at once. */
    ProcPool pool;
    pool_init(&pool);

    for (int i = 0; i < selected; i++) {
        if (!jobs[i].prepared) continue;

        /* A container build already has /src as its working directory, so the
           cwd only applies to the strategies that run on the host. */
        const char *cwd = jobs[i].container[0] ? NULL : jobs[i].work;

        Proc *proc = pool_spawn(&pool, jobs[i].target->id,
                                cwd, jobs[i].spawn.v, jobs[i].env.v);
        if (!proc) {
            snprintf(jobs[i].detail, sizeof jobs[i].detail,
                     "could not start the build");
            jobs[i].prepared = 0;
            continue;
        }
        proc->user = &jobs[i];
    }

    pool_wait_all(&pool);

    /* Phase 3: verify what was produced, then collect it. */
    for (int i = 0; i < pool.count; i++) {
        Proc *proc = &pool.procs[i];
        Job  *job  = proc->user;
        if (!job) continue;

        job->seconds = proc->finished - proc->started;

        if (proc->status != 0) {
            snprintf(job->detail, sizeof job->detail,
                     "%s exited %d", m->command, proc->status);
            continue;
        }
        if (verify_artifact(job, job->target, job->produced) != 0) continue;

        char dest_dir[1024];
        snprintf(dest_dir, sizeof dest_dir, "%s/%s",
                 o->dist_dir, job->target->id);
        if (make_dir(dest_dir) != 0) {
            snprintf(job->detail, sizeof job->detail,
                     "could not create %s", dest_dir);
            continue;
        }

        snprintf(job->shipped, sizeof job->shipped, "%s/%s",
                 dest_dir, target_artifact(m, job->target));

        if (copy_file(job->produced, job->shipped) != 0) {
            snprintf(job->detail, sizeof job->detail,
                     "could not copy the artifact into %s", dest_dir);
            continue;
        }
        job->ok = 1;

        /* Stored only after the artifact has been checked, so a rejected build
           can never be handed back on a later run. */
        if (job->has_key)
            cache_store(o, job->target, job->key,
                        target_artifact(m, job->target), job->produced);
    }

    double elapsed = now_seconds() - t0;

    /* Report. Successes stay one line each; failures print the build log,
       which is the only thing anyone wants to see at that point. */
    int failed = 0;
    putchar('\n');
    for (int i = 0; i < selected; i++) {
        Job *job = &jobs[i];

        if (job->ok && job->cached) {
            char size[32];
            human_size(job->size, size, sizeof size);
            printf("  ok    %-18s cached  %8s  %s\n",
                   job->target->id, size, job->detail);
        } else if (job->ok) {
            char size[32];
            human_size(job->size, size, sizeof size);
            printf("  ok    %-18s %6.2fs  %8s  %s\n",
                   job->target->id, job->seconds, size, job->detail);
        } else {
            failed++;
            printf("  FAIL  %-18s %6.2fs  %s\n",
                   job->target->id, job->seconds,
                   job->detail[0] ? job->detail : "build failed");
        }
    }

    if (o->verbose || failed) {
        for (int i = 0; i < pool.count; i++) {
            Proc *proc = &pool.procs[i];
            Job  *job  = proc->user;
            if (!job || (job->ok && !o->verbose)) continue;
            if (!proc->out.data || proc->out.len == 0) continue;

            printf("\n--- %s ---\n%s", job->target->id, proc->out.data);
            if (proc->out.data[proc->out.len - 1] != '\n') putchar('\n');
        }
    }

    printf("\n%d/%d succeeded in %.2fs → %s/\n",
           selected - failed, selected, elapsed, o->dist_dir);

    for (int i = 0; i < selected; i++) {
        av_free(&jobs[i].argv);
        av_free(&jobs[i].env);
        av_free(&jobs[i].spawn);
        container_stop(jobs[i].container);
    }
    pool_free(&pool);

    return failed;
}

int cmd_targets(const Manifest *m) {
    printf("%s %s\n\n", m->name, m->version);
    printf("  %-18s %-22s %-16s %s\n", "ID", "TRIPLE", "EXPECTED", "ARTIFACT");

    for (int i = 0; i < m->target_count; i++) {
        const Target *t = &m->targets[i];

        BinFormat fmt;
        BinArch   arch;
        char expected[48];

        if (binfmt_from_triple(t->triple, &fmt, &arch) == 0)
            snprintf(expected, sizeof expected, "%s %s",
                     binfmt_format_name(fmt), binfmt_arch_name(arch));
        else
            snprintf(expected, sizeof expected, "unknown");

        printf("  %-18s %-22s %-16s %s%s\n", t->id, t->triple, expected,
               target_artifact(m, t), t->verify ? "  (verify)" : "");
    }
    return 0;
}
