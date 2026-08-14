#include "../include/cache.h"
#include "../include/exec.h"
#include "../include/util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>

/* A growable, sortable list of relative paths. Directory order is not stable
   between filesystems or even between runs, so the list is sorted before it is
   hashed — otherwise the same tree could produce two different keys. */
typedef struct {
    char **v;
    int    count;
    int    cap;
} PathList;

static void pl_init(PathList *p) {
    p->v = NULL;
    p->count = 0;
    p->cap = 0;
}

static void pl_free(PathList *p) {
    for (int i = 0; i < p->count; i++) free(p->v[i]);
    free(p->v);
    pl_init(p);
}

static int pl_push(PathList *p, const char *s) {
    if (p->count + 1 > p->cap) {
        int cap = p->cap ? p->cap * 2 : 64;
        char **n = realloc(p->v, (size_t)cap * sizeof *n);
        if (!n) return -1;
        p->v = n;
        p->cap = cap;
    }
    char *copy = strdup(s);
    if (!copy) return -1;
    p->v[p->count++] = copy;
    return 0;
}

static int path_cmp(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static int is_excluded(const char *name, const BuildOpts *o) {
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return 1;
    if (strcmp(name, ".git") == 0) return 1;

    const char *w = strrchr(o->work_dir, '/');
    const char *d = strrchr(o->dist_dir, '/');
    w = w ? w + 1 : o->work_dir;
    d = d ? d + 1 : o->dist_dir;

    return strcmp(name, w) == 0 || strcmp(name, d) == 0;
}

/* Collects every regular file under `dir`, as paths relative to the root. */
static int walk(const char *root, const char *rel, const BuildOpts *o,
                PathList *out) {
    char dir[2048];
    if (rel[0]) snprintf(dir, sizeof dir, "%s/%s", root, rel);
    else        snprintf(dir, sizeof dir, "%s", root);

    DIR *d = opendir(dir);
    if (!d) return -1;

    int rc = 0;
    struct dirent *e;

    while ((e = readdir(d))) {
        /* Exclusions apply at the root only: a source directory legitimately
           named "dist" deeper in the tree is still an input. */
        if (!rel[0] && is_excluded(e->d_name, o)) continue;
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;

        char child[2048];
        if (rel[0]) snprintf(child, sizeof child, "%s/%s", rel, e->d_name);
        else        snprintf(child, sizeof child, "%s", e->d_name);

        char full[2048];
        snprintf(full, sizeof full, "%s/%s", root, child);

        struct stat st;
        if (lstat(full, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            if (walk(root, child, o, out) != 0) rc = -1;
        } else if (S_ISREG(st.st_mode)) {
            if (pl_push(out, child) != 0) rc = -1;
        }
        /* Symlinks are deliberately ignored: following them could wander
           outside the tree, and their targets are hashed anyway if they are
           inside it. */
    }

    closedir(d);
    return rc;
}

const char *toolchain_id(void) {
    static char id[128];
    static int  ready = 0;

    if (ready) return id;
    ready = 1;

    ArgV a;
    av_init(&a);
    av_push(&a, "zig");
    av_push(&a, "version");

    StrBuf out;
    sb_init(&out);
    int rc = run_capture(NULL, a.v, &out);
    av_free(&a);

    if (rc == 0 && out.data) {
        char *nl = strchr(out.data, '\n');
        if (nl) *nl = '\0';
        snprintf(id, sizeof id, "zig %s", out.data);
    } else {
        snprintf(id, sizeof id, "zig unknown");
    }

    sb_free(&out);
    return id;
}

int cache_key(const Manifest *m, const Target *t, const BuildOpts *o,
              char *const command[], char *const env[], const char *toolchain,
              char out_hex[SHA256_HEX_LEN]) {
    PathList files;
    pl_init(&files);

    if (walk(o->source_root, "", o, &files) != 0 || files.count == 0) {
        pl_free(&files);
        return -1;
    }

    qsort(files.v, (size_t)files.count, sizeof *files.v, path_cmp);

    Sha256 ctx;
    sha256_init(&ctx);

    /* Everything that is not file content goes in first, each part length-
       prefixed by its own newline so that concatenation cannot be ambiguous. */
    sha256_update(&ctx, "atom-cache-v1\n", 14);
    sha256_update(&ctx, toolchain, strlen(toolchain));
    sha256_update(&ctx, "\n", 1);
    sha256_update(&ctx, t->triple, strlen(t->triple));
    sha256_update(&ctx, "\n", 1);
    sha256_update(&ctx, target_artifact(m, t), strlen(target_artifact(m, t)));
    sha256_update(&ctx, "\n", 1);

    /* The container strategy's inputs live outside the build command, so they
       are hashed explicitly: a different image builds a different binary. */
    sha256_update(&ctx, m->system, strlen(m->system));
    sha256_update(&ctx, "\n", 1);
    sha256_update(&ctx, t->strategy, strlen(t->strategy));
    sha256_update(&ctx, "\n", 1);
    sha256_update(&ctx, t->image, strlen(t->image));
    sha256_update(&ctx, "\n", 1);
    sha256_update(&ctx, t->setup, strlen(t->setup));
    sha256_update(&ctx, "\n", 1);

    for (int i = 0; command[i]; i++) {
        sha256_update(&ctx, command[i], strlen(command[i]));
        sha256_update(&ctx, "\n", 1);
    }

    /* Some build systems select the target through the environment rather
       than through flags, so the same argv with a different GOARCH is a
       different build. */
    sha256_update(&ctx, "--env--\n", 8);
    for (int i = 0; env && env[i]; i++) {
        sha256_update(&ctx, env[i], strlen(env[i]));
        sha256_update(&ctx, "\n", 1);
    }

    sha256_update(&ctx, "--files--\n", 10);

    for (int i = 0; i < files.count; i++) {
        char full[2048];
        snprintf(full, sizeof full, "%s/%s", o->source_root, files.v[i]);

        FILE *f = fopen(full, "rb");
        if (!f) { pl_free(&files); return -1; }

        /* The path is part of the key: moving a file changes the build even
           when the bytes are identical. */
        sha256_update(&ctx, files.v[i], strlen(files.v[i]));
        sha256_update(&ctx, "\n", 1);

        unsigned char buf[8192];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0)
            sha256_update(&ctx, buf, n);

        int bad = ferror(f);
        fclose(f);
        if (bad) { pl_free(&files); return -1; }
    }

    pl_free(&files);

    unsigned char digest[SHA256_DIGEST_LEN];
    sha256_final(&ctx, digest);

    for (int i = 0; i < SHA256_DIGEST_LEN; i++)
        snprintf(out_hex + i * 2, 3, "%02x", digest[i]);

    return 0;
}

static void cache_paths(const BuildOpts *o, const Target *t,
                        char *dir, size_t dir_size,
                        char *keyfile, size_t keyfile_size) {
    snprintf(dir, dir_size, "%s/.cache/%s", o->work_dir, t->id);
    snprintf(keyfile, keyfile_size, "%s/key", dir);
}

int cache_lookup(const BuildOpts *o, const Target *t, const char *key,
                 const char *artifact, const char *dest) {
    char dir[1024], keyfile[1100];
    cache_paths(o, t, dir, sizeof dir, keyfile, sizeof keyfile);

    FILE *f = fopen(keyfile, "r");
    if (!f) return -1;

    char stored[SHA256_HEX_LEN] = "";
    if (!fgets(stored, sizeof stored, f)) { fclose(f); return -1; }
    fclose(f);

    stored[strcspn(stored, "\r\n")] = '\0';
    if (strcmp(stored, key) != 0) return -1;

    char cached[1200];
    snprintf(cached, sizeof cached, "%s/%s", dir, artifact);

    struct stat st;
    if (stat(cached, &st) != 0) return -1;

    ArgV a;
    av_init(&a);
    av_push(&a, "cp");
    av_push(&a, "-p");
    av_push(&a, cached);
    av_push(&a, dest);

    int rc = run_sync(NULL, a.v, NULL);
    av_free(&a);

    return rc == 0 ? 0 : -1;
}

void cache_store(const BuildOpts *o, const Target *t, const char *key,
                 const char *artifact, const char *source) {
    char dir[1024], keyfile[1100];
    cache_paths(o, t, dir, sizeof dir, keyfile, sizeof keyfile);

    char parent[1024];
    snprintf(parent, sizeof parent, "%s/.cache", o->work_dir);

    if (make_dir(parent) != 0 || make_dir(dir) != 0) return;

    char cached[1200];
    snprintf(cached, sizeof cached, "%s/%s", dir, artifact);

    ArgV a;
    av_init(&a);
    av_push(&a, "cp");
    av_push(&a, "-p");
    av_push(&a, source);
    av_push(&a, cached);

    int rc = run_sync(NULL, a.v, NULL);
    av_free(&a);

    if (rc != 0) return;

    /* The key is written only after the artifact is in place, so an interrupted
       store leaves a stale artifact with no key rather than a key promising an
       artifact that is not there. */
    FILE *f = fopen(keyfile, "w");
    if (!f) return;
    fprintf(f, "%s\n", key);
    fclose(f);
}
