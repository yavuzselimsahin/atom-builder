#ifndef BINFMT_H
#define BINFMT_H

/* Identifying a produced binary by reading its own header, rather than by
   trusting the build's exit status or shelling out to file(1). A build that
   silently links stale objects of the wrong architecture exits 0 and looks
   fine; only the artifact itself tells the truth. */

typedef enum {
    BF_UNKNOWN = 0,
    BF_ELF,
    BF_MACHO,
    BF_PE
} BinFormat;

typedef enum {
    BA_UNKNOWN = 0,
    BA_X86_64,
    BA_AARCH64,
    BA_ARM,
    BA_I386,
    BA_RISCV64,
    BA_PPC64
} BinArch;

typedef struct {
    BinFormat fmt;
    BinArch   arch;
    int       is_static;   /* meaningful for ELF; -1 when not determined */
} BinInfo;

/* Reads the header of `path`. Returns 0 when the format was recognised,
   -1 when the file could not be read or is not an executable image. */
int binfmt_probe(const char *path, BinInfo *out);

/* Derives the expected format and architecture from a zig target triple such
   as "aarch64-linux-musl" or "x86_64-windows-gnu". Returns 0 on success. */
int binfmt_from_triple(const char *triple, BinFormat *fmt, BinArch *arch);

const char *binfmt_format_name(BinFormat f);
const char *binfmt_arch_name(BinArch a);

/* Docker's spelling of an architecture ("linux/arm64"), or NULL when Docker
   has no platform for it. */
const char *binfmt_docker_platform(BinArch a);

/* Formats a probed binary as "static ELF aarch64" into out. */
void binfmt_describe(const BinInfo *info, char *out, unsigned long out_size);

#endif
