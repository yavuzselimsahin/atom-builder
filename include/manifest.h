#ifndef MANIFEST_H
#define MANIFEST_H

#include <stddef.h>

#define MAX_TARGETS      32
#define MAX_ID           64
#define MAX_TRIPLE       64
#define MAX_PATHLIKE    256

typedef struct {
    char id[MAX_ID];              /* from the section name: [target.<id>]   */
    char triple[MAX_TRIPLE];      /* zig target triple                      */
    char artifact[MAX_PATHLIKE];  /* overrides build.artifact when set      */
    char make_args[MAX_PATHLIKE]; /* extra `make` variables for this target */
    char format[16];              /* overrides package.format when set      */
    char strategy[16];            /* "zig" (default), "container", "native" */
    char image[128];              /* container strategy: the build image    */
    char setup[256];              /* container strategy: run before building */
    int  verify;                  /* insist this target be verified         */
} Target;

typedef struct {
    char name[MAX_ID];
    char version[MAX_ID];

    char system[16];              /* make, cargo, go, zig, dotnet, custom   */
    char command[MAX_PATHLIKE];   /* overrides the system's command         */
    char artifact[MAX_PATHLIKE];  /* the produced file's name               */
    char output[MAX_PATHLIKE];    /* overrides where the system puts it     */
    char clean[MAX_PATHLIKE];     /* overrides the system's clean command   */
    int  strip;                   /* apply the system's strip flag          */

    /* [package] */
    char format[16];              /* "tar.gz" or "zip"                      */
    char include[512];            /* extra files, space separated           */
    int  checksum;                /* write SHA256SUMS alongside the archives */

    /* [verify] */
    char verify_args[256];        /* arguments passed to the built binary    */
    char verify_expect[256];      /* substring the output must contain       */
    char verify_image[128];       /* container image for emulated Linux runs */
    char wine_image[128];         /* container image carrying wine           */
    int  verify_exit;             /* expected exit status                    */
    int  verify_timeout;          /* seconds before the run is killed        */

    /* [publish.ssh] — host may be empty, in which case path is local */
    char ssh_host[256];
    char ssh_path[512];

    /* [publish.github] */
    char gh_repo[256];            /* owner/name                             */
    char gh_tag[64];              /* defaults to v<version>                 */

    Target targets[MAX_TARGETS];
    int    target_count;
} Manifest;

/* Reads `path` into `m`. On failure prints a diagnostic naming the problem and
   returns -1. `source_root` is where the manifest lives; builds are cloned from
   there. */
int manifest_load(const char *path, Manifest *m);

/* Returns the artifact filename for a target, honouring the per-target
   override. Never returns NULL for a validated manifest. */
const char *target_artifact(const Manifest *m, const Target *t);

/* Looks a target up by id, or NULL. */
const Target *manifest_find(const Manifest *m, const char *id);

/* Effective archive format for a target: "tar.gz" or "zip". Windows targets
   default to zip; a target may override either way. */
const char *target_format(const Manifest *m, const Target *t);

/* "<name>-<version>-<id>", the directory inside the archive. */
void target_prefix(const Manifest *m, const Target *t, char *out, size_t size);

/* "<name>-<version>-<id>.tar.gz", the archive's own filename. */
void target_archive(const Manifest *m, const Target *t, char *out, size_t size);

/* Release tag: the manifest's, or v<version>. */
void manifest_tag(const Manifest *m, char *out, size_t size);

/* Resolves one of the manifest's build templates: the value set in [build] if
   there is one, otherwise the build system's own. `field` is one of
   "command", "output" or "clean". Returns "" when neither supplies one. */
const char *manifest_template(const Manifest *m, const char *field);

#endif
