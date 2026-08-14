#include "../include/build.h"
#include "../include/archive.h"
#include "../include/hash.h"
#include "../include/util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#define MAX_ENTRIES 32

typedef struct {
    const Target *target;
    char  archive[1024];     /* path written                        */
    char  base[256];         /* archive filename, for SHA256SUMS    */
    char  sha[SHA256_HEX_LEN];
    long long size;
    int   ok;
    char  detail[192];
} PackJob;

static const char *basename_of(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

int cmd_package(const Manifest *m, const BuildOpts *o) {
    PackJob jobs[MAX_TARGETS];
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

    /* Extra files ride along in every archive, resolved against the project
       root so a manifest can name README.md without a path. */
    ArgV extras;
    av_init(&extras);
    if (av_push_split(&extras, m->include) != 0) {
        fprintf(stderr, "atom: could not read [package] include\n");
        av_free(&extras);
        return 1;
    }

    printf("%s %s — packaging %d target%s\n", m->name, m->version,
           selected, selected == 1 ? "" : "s");

    double t0 = now_seconds();

    for (int i = 0; i < selected; i++) {
        PackJob      *job = &jobs[i];
        const Target *t   = job->target;
        const char   *art = target_artifact(m, t);

        char built[1024];
        snprintf(built, sizeof built, "%s/%s/%s", o->dist_dir, t->id, art);

        struct stat st;
        if (stat(built, &st) != 0) {
            snprintf(job->detail, sizeof job->detail,
                     "not built yet — run `atom build` first");
            continue;
        }

        ArchiveFormat fmt = strcmp(target_format(m, t), "zip") == 0
                          ? AR_ZIP : AR_TARGZ;

        /* atomik-ssg-0.3.0-linux-arm64/ inside, and .tar.gz or .zip outside. */
        char prefix[256];
        target_prefix(m, t, prefix, sizeof prefix);
        target_archive(m, t, job->base, sizeof job->base);
        snprintf(job->archive, sizeof job->archive, "%s/%s",
                 o->dist_dir, job->base);

        ArchiveEntry entries[MAX_ENTRIES];
        char         names[MAX_ENTRIES][512];
        char         srcs[MAX_ENTRIES][1024];
        int          n = 0;

        snprintf(srcs[n],  sizeof srcs[n],  "%s", built);
        snprintf(names[n], sizeof names[n], "%s/%s", prefix, art);
        entries[n].src  = srcs[n];
        entries[n].name = names[n];
        n++;

        int missing = 0;
        for (int k = 0; k < extras.count && n < MAX_ENTRIES; k++) {
            snprintf(srcs[n], sizeof srcs[n], "%s/%s",
                     o->source_root, extras.v[k]);
            if (stat(srcs[n], &st) != 0) {
                snprintf(job->detail, sizeof job->detail,
                         "[package] include names %s, which does not exist",
                         extras.v[k]);
                missing = 1;
                break;
            }
            snprintf(names[n], sizeof names[n], "%s/%s", prefix,
                     basename_of(extras.v[k]));
            entries[n].src  = srcs[n];
            entries[n].name = names[n];
            n++;
        }
        if (missing) continue;

        if (archive_write(job->archive, fmt, entries, n) != 0) {
            snprintf(job->detail, sizeof job->detail, "could not write %s",
                     job->base);
            continue;
        }

        if (stat(job->archive, &st) == 0) job->size = (long long)st.st_size;

        if (sha256_file(job->archive, job->sha) != 0) {
            snprintf(job->detail, sizeof job->detail,
                     "could not hash %s", job->base);
            continue;
        }

        snprintf(job->detail, sizeof job->detail, "%d file%s",
                 n, n == 1 ? "" : "s");
        job->ok = 1;
    }

    av_free(&extras);

    int failed = 0;
    putchar('\n');
    for (int i = 0; i < selected; i++) {
        PackJob *job = &jobs[i];
        if (job->ok) {
            char size[32];
            human_size(job->size, size, sizeof size);
            printf("  ok    %-34s %9s  %s\n", job->base, size, job->detail);
        } else {
            failed++;
            printf("  FAIL  %-34s %s\n", job->target->id, job->detail);
        }
    }

    /* One SHA256SUMS for the whole release, in the format sha256sum -c reads. */
    if (m->checksum && failed < selected) {
        char sums[1024];
        snprintf(sums, sizeof sums, "%s/SHA256SUMS", o->dist_dir);

        FILE *f = fopen(sums, "w");
        if (!f) {
            fprintf(stderr, "atom: %s: %s\n", sums, strerror(errno));
            failed++;
        } else {
            for (int i = 0; i < selected; i++)
                if (jobs[i].ok)
                    fprintf(f, "%s  %s\n", jobs[i].sha, jobs[i].base);

            if (fclose(f) != 0) {
                fprintf(stderr, "atom: could not write %s\n", sums);
                failed++;
            } else {
                printf("  ok    %-34s %9s  sha256\n", "SHA256SUMS", "");
            }
        }
    }

    printf("\n%d/%d packaged in %.2fs → %s/\n",
           selected - failed, selected, now_seconds() - t0, o->dist_dir);

    return failed;
}
