#include "../include/util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

#define SB_MIN_CAP 256

void sb_init(StrBuf *sb) {
    sb->data = NULL;
    sb->len  = 0;
    sb->cap  = 0;
}

void sb_free(StrBuf *sb) {
    free(sb->data);
    sb_init(sb);
}

int sb_reserve(StrBuf *sb, size_t extra) {
    size_t need = sb->len + extra + 1;
    if (need <= sb->cap) return 0;

    size_t cap = sb->cap ? sb->cap : SB_MIN_CAP;
    while (cap < need) {
        if (cap > (size_t)-1 / 2) return -1;
        cap *= 2;
    }

    char *p = realloc(sb->data, cap);
    if (!p) return -1;

    sb->data = p;
    sb->cap  = cap;
    return 0;
}

int sb_append_n(StrBuf *sb, const char *s, size_t n) {
    if (sb_reserve(sb, n) != 0) return -1;
    memcpy(sb->data + sb->len, s, n);
    sb->len += n;
    sb->data[sb->len] = '\0';
    return 0;
}

int sb_append(StrBuf *sb, const char *s) {
    return sb_append_n(sb, s, strlen(s));
}

int sb_appendf(StrBuf *sb, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) return -1;

    if (sb_reserve(sb, (size_t)n) != 0) return -1;

    va_start(ap, fmt);
    vsnprintf(sb->data + sb->len, (size_t)n + 1, fmt, ap);
    va_end(ap);

    sb->len += (size_t)n;
    return 0;
}

/* --------------------------------------------------------------------- */

void av_init(ArgV *a) {
    a->v     = NULL;
    a->count = 0;
    a->cap   = 0;
}

void av_free(ArgV *a) {
    for (int i = 0; i < a->count; i++) free(a->v[i]);
    free(a->v);
    av_init(a);
}

/* Keeps one trailing NULL slot so a->v is always a valid argv. */
static int av_grow(ArgV *a) {
    if (a->count + 2 <= a->cap) return 0;

    int cap = a->cap ? a->cap * 2 : 8;
    char **p = realloc(a->v, (size_t)cap * sizeof *p);
    if (!p) return -1;

    a->v   = p;
    a->cap = cap;
    return 0;
}

int av_push(ArgV *a, const char *s) {
    if (av_grow(a) != 0) return -1;

    char *copy = strdup(s);
    if (!copy) return -1;

    a->v[a->count++] = copy;
    a->v[a->count]   = NULL;
    return 0;
}

int av_pushf(ArgV *a, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) return -1;

    char *buf = malloc((size_t)n + 1);
    if (!buf) return -1;

    va_start(ap, fmt);
    vsnprintf(buf, (size_t)n + 1, fmt, ap);
    va_end(ap);

    int rc = av_push(a, buf);
    free(buf);
    return rc;
}

int av_push_split(ArgV *a, const char *s) {
    if (!s) return 0;

    StrBuf field;
    sb_init(&field);

    int in_field = 0;
    char quote   = 0;

    for (const char *p = s; ; p++) {
        if (quote) {
            if (*p == '\0') { sb_free(&field); return -1; }  /* unterminated */
            if (*p == quote) { quote = 0; continue; }
            if (sb_append_n(&field, p, 1) != 0) { sb_free(&field); return -1; }
            continue;
        }

        if (*p == '"' || *p == '\'') {
            quote    = *p;
            in_field = 1;
            continue;
        }

        if (*p == '\0' || *p == ' ' || *p == '\t') {
            if (in_field) {
                if (av_push(a, field.data ? field.data : "") != 0) {
                    sb_free(&field);
                    return -1;
                }
                field.len = 0;
                if (field.data) field.data[0] = '\0';
                in_field = 0;
            }
            if (*p == '\0') break;
            continue;
        }

        in_field = 1;
        if (sb_append_n(&field, p, 1) != 0) { sb_free(&field); return -1; }
    }

    sb_free(&field);
    return 0;
}

/* --------------------------------------------------------------------- */

int make_dir(const char *path) {
    if (mkdir(path, 0755) == 0) return 0;
    return (errno == EEXIST) ? 0 : -1;
}

int remove_tree(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0)
        return (errno == ENOENT) ? 0 : -1;

    if (!S_ISDIR(st.st_mode))
        return unlink(path);

    DIR *d = opendir(path);
    if (!d) return -1;

    int rc = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;

        char child[4096];
        int n = snprintf(child, sizeof child, "%s/%s", path, e->d_name);
        if (n < 0 || n >= (int)sizeof child) { rc = -1; continue; }

        if (remove_tree(child) != 0) rc = -1;
    }
    closedir(d);

    if (rmdir(path) != 0) rc = -1;
    return rc;
}

int cpu_count(void) {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return (n > 0) ? (int)n : 1;
}

double now_seconds(void) {
    struct timespec ts;
#if defined(CLOCK_MONOTONIC)
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
        return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
#endif
    return (double)time(NULL);
}

void human_size(long long bytes, char *out, size_t out_size) {
    if (bytes < 1024)
        snprintf(out, out_size, "%lld B", bytes);
    else if (bytes < 1024LL * 1024)
        snprintf(out, out_size, "%.0f KB", (double)bytes / 1024.0);
    else
        snprintf(out, out_size, "%.1f MB", (double)bytes / (1024.0 * 1024.0));
}
