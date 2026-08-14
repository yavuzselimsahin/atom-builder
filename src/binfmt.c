#include "../include/binfmt.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* ELF e_machine values. */
#define EM_386     0x03
#define EM_ARM     0x28
#define EM_X86_64  0x3E
#define EM_AARCH64 0xB7
#define EM_PPC64   0x15
#define EM_RISCV   0xF3

/* Mach-O. CPU_ARCH_ABI64 (0x01000000) or'd into the base type. */
#define MH_MAGIC_64   0xFEEDFACFu
#define MH_MAGIC_32   0xFEEDFACEu
#define CPU_X86_64    0x01000007u
#define CPU_ARM64     0x0100000Cu
#define CPU_X86       0x00000007u
#define CPU_ARM       0x0000000Cu

/* PE IMAGE_FILE_MACHINE values. */
#define PE_AMD64  0x8664
#define PE_ARM64  0xAA64
#define PE_I386   0x014C
#define PE_ARMNT  0x01C4

#define PT_INTERP 3

static uint16_t le16(const unsigned char *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t le32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t le64(const unsigned char *p) {
    return (uint64_t)le32(p) | ((uint64_t)le32(p + 4) << 32);
}

static BinArch elf_arch(uint16_t machine) {
    switch (machine) {
        case EM_X86_64:  return BA_X86_64;
        case EM_AARCH64: return BA_AARCH64;
        case EM_ARM:     return BA_ARM;
        case EM_386:     return BA_I386;
        case EM_RISCV:   return BA_RISCV64;
        case EM_PPC64:   return BA_PPC64;
        default:         return BA_UNKNOWN;
    }
}

/* An ELF is dynamically linked exactly when it carries a PT_INTERP segment.
   Checking e_type would misread static-PIE binaries, which are ET_DYN. */
static int elf_is_static(FILE *f, const unsigned char *hdr) {
    if (hdr[4] != 2) return -1;          /* only ELF64 is inspected */

    uint64_t phoff     = le64(hdr + 32);
    uint16_t phentsize = le16(hdr + 54);
    uint16_t phnum     = le16(hdr + 56);

    if (phoff == 0 || phentsize < 4 || phnum == 0 || phnum > 512) return -1;

    for (uint16_t i = 0; i < phnum; i++) {
        unsigned char ph[4];
        if (fseek(f, (long)(phoff + (uint64_t)i * phentsize), SEEK_SET) != 0)
            return -1;
        if (fread(ph, 1, sizeof ph, f) != sizeof ph) return -1;
        if (le32(ph) == PT_INTERP) return 0;
    }
    return 1;
}

int binfmt_probe(const char *path, BinInfo *out) {
    out->fmt       = BF_UNKNOWN;
    out->arch      = BA_UNKNOWN;
    out->is_static = -1;

    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    unsigned char hdr[64];
    size_t n = fread(hdr, 1, sizeof hdr, f);
    if (n < 8) { fclose(f); return -1; }

    /* ELF */
    if (n >= 20 && hdr[0] == 0x7F && hdr[1] == 'E' &&
        hdr[2] == 'L' && hdr[3] == 'F') {
        out->fmt  = BF_ELF;
        out->arch = elf_arch(le16(hdr + 18));   /* little-endian targets only */
        if (n >= 58) out->is_static = elf_is_static(f, hdr);
        fclose(f);
        return 0;
    }

    /* Mach-O. Fat binaries are not unpacked here; a per-target build never
       produces one, and treating it as unknown is the honest answer. */
    if (n >= 8) {
        uint32_t magic = le32(hdr);
        if (magic == MH_MAGIC_64 || magic == MH_MAGIC_32) {
            out->fmt = BF_MACHO;
            switch (le32(hdr + 4)) {
                case CPU_X86_64: out->arch = BA_X86_64;  break;
                case CPU_ARM64:  out->arch = BA_AARCH64; break;
                case CPU_X86:    out->arch = BA_I386;    break;
                case CPU_ARM:    out->arch = BA_ARM;     break;
                default:         out->arch = BA_UNKNOWN; break;
            }
            fclose(f);
            return 0;
        }
    }

    /* PE: an MZ stub whose e_lfanew points at the PE signature. */
    if (n >= 0x40 && hdr[0] == 'M' && hdr[1] == 'Z') {
        uint32_t off = le32(hdr + 0x3C);
        unsigned char pe[6];

        if (fseek(f, (long)off, SEEK_SET) == 0 &&
            fread(pe, 1, sizeof pe, f) == sizeof pe &&
            pe[0] == 'P' && pe[1] == 'E' && pe[2] == 0 && pe[3] == 0) {
            out->fmt = BF_PE;
            switch (le16(pe + 4)) {
                case PE_AMD64: out->arch = BA_X86_64;  break;
                case PE_ARM64: out->arch = BA_AARCH64; break;
                case PE_I386:  out->arch = BA_I386;    break;
                case PE_ARMNT: out->arch = BA_ARM;     break;
                default:       out->arch = BA_UNKNOWN; break;
            }
            fclose(f);
            return 0;
        }
    }

    fclose(f);
    return -1;
}

int binfmt_from_triple(const char *triple, BinFormat *fmt, BinArch *arch) {
    if (!triple || !*triple) return -1;

    *fmt  = BF_UNKNOWN;
    *arch = BA_UNKNOWN;

    if      (strncmp(triple, "x86_64-",   7) == 0) *arch = BA_X86_64;
    else if (strncmp(triple, "aarch64-",  8) == 0) *arch = BA_AARCH64;
    else if (strncmp(triple, "arm-",      4) == 0) *arch = BA_ARM;
    else if (strncmp(triple, "thumb-",    6) == 0) *arch = BA_ARM;
    else if (strncmp(triple, "x86-",      4) == 0) *arch = BA_I386;
    else if (strncmp(triple, "i386-",     5) == 0) *arch = BA_I386;
    else if (strncmp(triple, "riscv64-",  8) == 0) *arch = BA_RISCV64;
    else if (strncmp(triple, "powerpc64-",10) == 0) *arch = BA_PPC64;

    if      (strstr(triple, "-linux"))   *fmt = BF_ELF;
    else if (strstr(triple, "-macos"))   *fmt = BF_MACHO;
    else if (strstr(triple, "-darwin"))  *fmt = BF_MACHO;
    else if (strstr(triple, "-windows")) *fmt = BF_PE;
    else if (strstr(triple, "-freebsd")) *fmt = BF_ELF;
    else if (strstr(triple, "-netbsd"))  *fmt = BF_ELF;
    else if (strstr(triple, "-openbsd")) *fmt = BF_ELF;

    return (*fmt == BF_UNKNOWN || *arch == BA_UNKNOWN) ? -1 : 0;
}

const char *binfmt_format_name(BinFormat f) {
    switch (f) {
        case BF_ELF:   return "ELF";
        case BF_MACHO: return "Mach-O";
        case BF_PE:    return "PE";
        default:       return "unknown";
    }
}

const char *binfmt_arch_name(BinArch a) {
    switch (a) {
        case BA_X86_64:  return "x86_64";
        case BA_AARCH64: return "aarch64";
        case BA_ARM:     return "arm";
        case BA_I386:    return "i386";
        case BA_RISCV64: return "riscv64";
        case BA_PPC64:   return "ppc64";
        default:         return "unknown";
    }
}

const char *binfmt_docker_platform(BinArch a) {
    switch (a) {
        case BA_X86_64:  return "linux/amd64";
        case BA_AARCH64: return "linux/arm64";
        case BA_ARM:     return "linux/arm/v7";
        case BA_I386:    return "linux/386";
        case BA_PPC64:   return "linux/ppc64le";
        case BA_RISCV64: return "linux/riscv64";
        default:         return NULL;
    }
}

void binfmt_describe(const BinInfo *info, char *out, unsigned long out_size) {
    const char *link = "";
    if (info->fmt == BF_ELF && info->is_static == 1) link = "static ";
    if (info->fmt == BF_ELF && info->is_static == 0) link = "dynamic ";

    snprintf(out, (size_t)out_size, "%s%s %s", link,
             binfmt_format_name(info->fmt), binfmt_arch_name(info->arch));
}
