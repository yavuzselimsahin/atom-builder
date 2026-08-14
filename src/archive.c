#include "../include/archive.h"
#include "../include/exec.h"
#include "../include/hash.h"
#include "../include/util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>

#define TAR_BLOCK   512
#define TAR_NAME_MAX 99   /* the ustar name field, less its terminator */

/* --------------------------------------------------------------------- */
/* tar                                                                     */
/* --------------------------------------------------------------------- */

/* ustar numeric fields are octal digits followed by a NUL, right-aligned and
   zero-padded across `width` bytes including that terminator. */
static void tar_octal(char *dst, size_t width, unsigned long long v) {
    snprintf(dst, width, "%0*llo", (int)(width - 1), v);
}

static int tar_add(FILE *out, const ArchiveEntry *e) {
    struct stat st;
    if (stat(e->src, &st) != 0) {
        fprintf(stderr, "atom: %s: %s\n", e->src, strerror(errno));
        return -1;
    }
    if (strlen(e->name) > TAR_NAME_MAX) {
        fprintf(stderr, "atom: archive path too long: %s\n", e->name);
        return -1;
    }

    char hdr[TAR_BLOCK];
    memset(hdr, 0, sizeof hdr);

    memcpy(hdr, e->name, strlen(e->name));
    tar_octal(hdr + 100,  8, (unsigned long long)(st.st_mode & 07777));
    tar_octal(hdr + 108,  8, 0);                       /* uid  */
    tar_octal(hdr + 116,  8, 0);                       /* gid  */
    tar_octal(hdr + 124, 12, (unsigned long long)st.st_size);
    tar_octal(hdr + 136, 12, (unsigned long long)st.st_mtime);

    memset(hdr + 148, ' ', 8);                         /* checksum placeholder */
    hdr[156] = '0';                                    /* regular file         */
    memcpy(hdr + 257, "ustar", 5);
    memcpy(hdr + 263, "00", 2);
    memcpy(hdr + 265, "root", 4);
    memcpy(hdr + 297, "root", 4);

    /* The checksum is the sum of every header byte with its own field read as
       spaces, written as six octal digits, a NUL, then a space. */
    unsigned sum = 0;
    for (int i = 0; i < TAR_BLOCK; i++) sum += (unsigned char)hdr[i];
    snprintf(hdr + 148, 8, "%06o", sum);
    hdr[154] = '\0';
    hdr[155] = ' ';

    if (fwrite(hdr, 1, TAR_BLOCK, out) != TAR_BLOCK) return -1;

    FILE *in = fopen(e->src, "rb");
    if (!in) return -1;

    char   buf[8192];
    size_t n, written = 0;
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { fclose(in); return -1; }
        written += n;
    }
    fclose(in);

    size_t pad = (TAR_BLOCK - (written % TAR_BLOCK)) % TAR_BLOCK;
    if (pad) {
        char zeros[TAR_BLOCK];
        memset(zeros, 0, pad);
        if (fwrite(zeros, 1, pad, out) != pad) return -1;
    }
    return 0;
}

static int write_targz(const char *out_path, const ArchiveEntry *entries,
                       int count) {
    /* out_path ends in .tar.gz; gzip turns <x>.tar into <x>.tar.gz itself. */
    size_t len = strlen(out_path);
    if (len < 4 || strcmp(out_path + len - 3, ".gz") != 0) {
        fprintf(stderr, "atom: %s does not end in .gz\n", out_path);
        return -1;
    }

    char tar_path[1024];
    snprintf(tar_path, sizeof tar_path, "%.*s", (int)(len - 3), out_path);

    FILE *out = fopen(tar_path, "wb");
    if (!out) {
        fprintf(stderr, "atom: %s: %s\n", tar_path, strerror(errno));
        return -1;
    }

    int rc = 0;
    for (int i = 0; i < count && rc == 0; i++)
        rc = tar_add(out, &entries[i]);

    if (rc == 0) {
        char zeros[TAR_BLOCK * 2];
        memset(zeros, 0, sizeof zeros);
        if (fwrite(zeros, 1, sizeof zeros, out) != sizeof zeros) rc = -1;
    }

    if (fclose(out) != 0) rc = -1;
    if (rc != 0) { remove(tar_path); return -1; }

    remove(out_path);   /* gzip refuses to overwrite silently */

    ArgV a;
    av_init(&a);
    av_push(&a, "gzip");
    av_push(&a, "-9");
    av_push(&a, "-n");     /* omit the name and timestamp, for reproducibility */
    av_push(&a, "-f");
    av_push(&a, tar_path);

    rc = run_sync(NULL, a.v, NULL);
    av_free(&a);

    if (rc != 0) {
        fprintf(stderr, "atom: gzip failed on %s\n", tar_path);
        remove(tar_path);
        return -1;
    }
    return 0;
}

/* --------------------------------------------------------------------- */
/* zip                                                                     */
/* --------------------------------------------------------------------- */

static void put16(unsigned char *p, unsigned v) {
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
}

static void put32(unsigned char *p, unsigned long v) {
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

static unsigned long rd32(const unsigned char *p) {
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
           ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

/* Compresses `path` and returns the bare DEFLATE stream.

   gzip already wraps exactly the stream zip wants: strip its 10-byte header
   (plus whichever optional fields the flag byte announces) and its 8-byte
   trailer, and what remains is the compressed data. The trailer also carries
   the CRC-32 and uncompressed size that the zip entry needs. */
static int deflate_file(const char *path, StrBuf *raw, unsigned long *crc,
                        unsigned long *isize) {
    StrBuf gz;
    sb_init(&gz);

    ArgV a;
    av_init(&a);
    av_push(&a, "gzip");
    av_push(&a, "-9");
    av_push(&a, "-n");
    av_push(&a, "-c");
    av_push(&a, path);

    int rc = run_capture(NULL, a.v, &gz);
    av_free(&a);

    if (rc != 0 || gz.len < 18) {
        fprintf(stderr, "atom: gzip failed on %s\n", path);
        sb_free(&gz);
        return -1;
    }

    const unsigned char *p = (const unsigned char *)gz.data;
    if (p[0] != 0x1F || p[1] != 0x8B || p[2] != 8) {
        fprintf(stderr, "atom: gzip produced an unexpected stream\n");
        sb_free(&gz);
        return -1;
    }

    unsigned flg = p[3];
    size_t   off = 10;

    if (flg & 0x04) {                            /* FEXTRA */
        if (off + 2 > gz.len) goto malformed;
        off += 2 + ((size_t)p[off] | ((size_t)p[off + 1] << 8));
    }
    if (flg & 0x08) {                            /* FNAME */
        while (off < gz.len && p[off] != 0) off++;
        off++;
    }
    if (flg & 0x10) {                            /* FCOMMENT */
        while (off < gz.len && p[off] != 0) off++;
        off++;
    }
    if (flg & 0x02) off += 2;                    /* FHCRC */

    if (off + 8 > gz.len) goto malformed;

    *crc   = rd32(p + gz.len - 8);
    *isize = rd32(p + gz.len - 4);

    if (sb_append_n(raw, gz.data + off, gz.len - 8 - off) != 0) {
        sb_free(&gz);
        return -1;
    }

    sb_free(&gz);
    return 0;

malformed:
    fprintf(stderr, "atom: could not parse gzip output for %s\n", path);
    sb_free(&gz);
    return -1;
}

static void dos_time(time_t t, unsigned *dos_date, unsigned *dos_time) {
    struct tm tm;
    localtime_r(&t, &tm);

    int year = tm.tm_year + 1900;
    if (year < 1980) year = 1980;

    *dos_date = (unsigned)(((year - 1980) << 9) | ((tm.tm_mon + 1) << 5) |
                           tm.tm_mday);
    *dos_time = (unsigned)((tm.tm_hour << 11) | (tm.tm_min << 5) |
                           (tm.tm_sec / 2));
}

typedef struct {
    unsigned long offset;
    unsigned long crc;
    unsigned long csize;
    unsigned long usize;
    unsigned      dos_date;
    unsigned      dos_time;
    unsigned long mode;
    const char   *name;
} ZipEntry;

static int write_zip(const char *out_path, const ArchiveEntry *entries,
                     int count) {
    FILE *out = fopen(out_path, "wb");
    if (!out) {
        fprintf(stderr, "atom: %s: %s\n", out_path, strerror(errno));
        return -1;
    }

    ZipEntry *index = calloc((size_t)count, sizeof *index);
    if (!index) { fclose(out); return -1; }

    unsigned long offset = 0;
    int rc = 0;

    for (int i = 0; i < count && rc == 0; i++) {
        struct stat st;
        if (stat(entries[i].src, &st) != 0) {
            fprintf(stderr, "atom: %s: %s\n", entries[i].src, strerror(errno));
            rc = -1;
            break;
        }

        StrBuf data;
        sb_init(&data);

        unsigned long crc = 0, usize = 0;
        if (deflate_file(entries[i].src, &data, &crc, &usize) != 0) {
            sb_free(&data);
            rc = -1;
            break;
        }

        ZipEntry *z = &index[i];
        z->offset = offset;
        z->crc    = crc;
        z->csize  = (unsigned long)data.len;
        z->usize  = usize;
        z->mode   = (unsigned long)(st.st_mode & 0xFFFF);
        z->name   = entries[i].name;
        dos_time(st.st_mtime, &z->dos_date, &z->dos_time);

        size_t namelen = strlen(z->name);

        unsigned char lh[30];
        memset(lh, 0, sizeof lh);
        put32(lh,      0x04034B50);
        put16(lh + 4,  20);             /* version needed */
        put16(lh + 6,  0);              /* flags          */
        put16(lh + 8,  8);              /* deflate        */
        put16(lh + 10, z->dos_time);
        put16(lh + 12, z->dos_date);
        put32(lh + 14, z->crc);
        put32(lh + 18, z->csize);
        put32(lh + 22, z->usize);
        put16(lh + 26, (unsigned)namelen);
        put16(lh + 28, 0);

        if (fwrite(lh, 1, sizeof lh, out) != sizeof lh ||
            fwrite(z->name, 1, namelen, out) != namelen ||
            fwrite(data.data, 1, data.len, out) != data.len) {
            rc = -1;
        }

        offset += (unsigned long)(sizeof lh + namelen + data.len);
        sb_free(&data);
    }

    unsigned long cd_offset = offset;
    unsigned long cd_size   = 0;

    for (int i = 0; i < count && rc == 0; i++) {
        ZipEntry *z = &index[i];
        size_t namelen = strlen(z->name);

        unsigned char ch[46];
        memset(ch, 0, sizeof ch);
        put32(ch,      0x02014B50);
        put16(ch + 4,  0x031E);         /* made by unix, zip 3.0 */
        put16(ch + 6,  20);
        put16(ch + 8,  0);
        put16(ch + 10, 8);
        put16(ch + 12, z->dos_time);
        put16(ch + 14, z->dos_date);
        put32(ch + 16, z->crc);
        put32(ch + 20, z->csize);
        put32(ch + 24, z->usize);
        put16(ch + 28, (unsigned)namelen);
        put32(ch + 38, z->mode << 16);  /* unix permissions            */
        put32(ch + 42, z->offset);

        if (fwrite(ch, 1, sizeof ch, out) != sizeof ch ||
            fwrite(z->name, 1, namelen, out) != namelen) {
            rc = -1;
        }
        cd_size += (unsigned long)(sizeof ch + namelen);
    }

    if (rc == 0) {
        unsigned char eocd[22];
        memset(eocd, 0, sizeof eocd);
        put32(eocd,      0x06054B50);
        put16(eocd + 8,  (unsigned)count);
        put16(eocd + 10, (unsigned)count);
        put32(eocd + 12, cd_size);
        put32(eocd + 16, cd_offset);

        if (fwrite(eocd, 1, sizeof eocd, out) != sizeof eocd) rc = -1;
    }

    free(index);
    if (fclose(out) != 0) rc = -1;
    if (rc != 0) remove(out_path);
    return rc;
}

/* --------------------------------------------------------------------- */

int archive_write(const char *out_path, ArchiveFormat fmt,
                  const ArchiveEntry *entries, int count) {
    if (count <= 0) {
        fprintf(stderr, "atom: nothing to archive into %s\n", out_path);
        return -1;
    }
    return fmt == AR_ZIP ? write_zip(out_path, entries, count)
                         : write_targz(out_path, entries, count);
}
