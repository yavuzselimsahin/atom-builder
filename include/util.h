#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>

/* Growable string buffer. Zero-initialised via sb_init(). */
typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} StrBuf;

void sb_init(StrBuf *sb);
void sb_free(StrBuf *sb);
int  sb_reserve(StrBuf *sb, size_t extra);
int  sb_append_n(StrBuf *sb, const char *s, size_t n);
int  sb_append(StrBuf *sb, const char *s);
int  sb_appendf(StrBuf *sb, const char *fmt, ...);

/* NULL-terminated, heap-allocated argument vector. */
typedef struct {
    char **v;
    int    count;
    int    cap;
} ArgV;

void av_init(ArgV *a);
void av_free(ArgV *a);
/* Copies s. Returns -1 on allocation failure. */
int  av_push(ArgV *a, const char *s);
int  av_pushf(ArgV *a, const char *fmt, ...);
/* Splits s on whitespace, honouring "..." and '...' grouping, and pushes each
   field. This is what keeps `make` invocations off /bin/sh: a manifest value
   like `BIN=a.exe LIBS=-lws2_32` becomes two argv entries, and a value that
   contains spaces stays one. */
int  av_push_split(ArgV *a, const char *s);

/* mkdir() that succeeds if the directory already exists. */
int make_dir(const char *path);

/* Recursive rm -rf. Returns 0 on success. */
int remove_tree(const char *path);

/* Number of usable cores, or 1 if it cannot be determined. */
int cpu_count(void);

/* Monotonic seconds since an arbitrary epoch, for timing. */
double now_seconds(void);

/* Human-readable byte count into a caller-supplied buffer ("476 KB"). */
void human_size(long long bytes, char *out, size_t out_size);

#endif
