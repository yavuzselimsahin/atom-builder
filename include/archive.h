#ifndef ARCHIVE_H
#define ARCHIVE_H

typedef enum {
    AR_TARGZ,
    AR_ZIP
} ArchiveFormat;

typedef struct {
    const char *src;   /* file on disk                        */
    const char *name;  /* path recorded inside the archive    */
} ArchiveEntry;

/* Writes `entries` to `out_path`. Both formats are produced here rather than
   by an archiving library: tar is 512-byte headers, and zip is a container
   around DEFLATE streams. The compression itself comes from gzip(1), which is
   present wherever atom runs, so no compression library is linked in. */
int archive_write(const char *out_path, ArchiveFormat fmt,
                  const ArchiveEntry *entries, int count);

#endif
