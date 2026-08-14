#include "../include/buildsys.h"
#include "../include/binfmt.h"

#include <stdio.h>
#include <string.h>

/* The table. Adding a build system means adding a row.

   Verified against a real project: make, zig, dotnet, custom.
   Written from each tool's documented interface but not exercised here,
   because the toolchain is not installed on this machine: cargo, go.
   See ROADMAP.md. */
static const BuildSystem SYSTEMS[] = {
    {
        .name       = "make",
        .command    = "make -j{jobs}",
        .output     = "{artifact}",
        .clean      = "make clean",
        .env        = "",
        .cc_var     = "CC=zig cc -target {triple}",
        .strip_flag = "LDFLAGS=-s",
    },
    {
        .name       = "cargo",
        .command    = "cargo build --release --target {triple} --jobs {jobs}",
        .output     = "target/{triple}/release/{artifact}",
        .clean      = "",
        .env        = "",
        .cc_var     = "",
        /* Cargo strips through the release profile rather than a flag, so
           `strip = true` in Cargo.toml is the equivalent knob. */
        .strip_flag = "",
    },
    {
        .name       = "go",
        .command    = "go build -trimpath -o {artifact}",
        .output     = "{artifact}",
        .clean      = "",
        .env        = "GOOS={goos} GOARCH={goarch} CGO_ENABLED=0",
        .cc_var     = "",
        .strip_flag = "\"-ldflags=-s -w\"",
    },
    {
        .name       = "zig",
        .command    = "zig build -Dtarget={triple}",
        .output     = "zig-out/bin/{artifact}",
        .clean      = "",
        .env        = "",
        .cc_var     = "",
        /* Optimisation is build.zig's decision, not atom's. */
        .strip_flag = "",
    },
    {
        .name       = "dotnet",
        /* PublishSingleFile is not a preference here, it is what makes the
           output an artifact at all: a plain self-contained publish is a
           directory of runtime assemblies, and atom ships files. */
        .command    = "dotnet publish -c Release -r {rid} --self-contained true"
                      " -p:PublishSingleFile=true -o .atom-out",
        .output     = ".atom-out/{artifact}",
        .clean      = "",
        .env        = "",
        .cc_var     = "",
        .strip_flag = "",
    },
    {
        /* Everything comes from the manifest. The escape hatch for build
           systems atom has never heard of. */
        .name       = "custom",
        .command    = "",
        .output     = "",
        .clean      = "",
        .env        = "",
        .cc_var     = "",
        .strip_flag = "",
    },
};

#define SYSTEM_COUNT ((int)(sizeof SYSTEMS / sizeof SYSTEMS[0]))

const BuildSystem *buildsys_find(const char *name) {
    for (int i = 0; i < SYSTEM_COUNT; i++)
        if (strcmp(SYSTEMS[i].name, name) == 0) return &SYSTEMS[i];
    return NULL;
}

const char *buildsys_names(void) {
    static char list[128];
    if (list[0]) return list;

    size_t n = 0;
    for (int i = 0; i < SYSTEM_COUNT; i++) {
        int written = snprintf(list + n, sizeof list - n, "%s%s",
                               i ? ", " : "", SYSTEMS[i].name);
        if (written < 0 || (size_t)written >= sizeof list - n) break;
        n += (size_t)written;
    }
    return list;
}

/* --------------------------------------------------------------------- */

/* Each toolchain spells the same platform differently. Rather than asking the
   user to know all of them, the triple is the single source of truth and the
   rest are derived from it. */

static const char *os_of(const char *triple) {
    if (strstr(triple, "-linux"))   return "linux";
    if (strstr(triple, "-macos"))   return "macos";
    if (strstr(triple, "-darwin"))  return "macos";
    if (strstr(triple, "-windows")) return "windows";
    if (strstr(triple, "-freebsd")) return "freebsd";
    if (strstr(triple, "-netbsd"))  return "netbsd";
    if (strstr(triple, "-openbsd")) return "openbsd";
    return NULL;
}

static const char *goos_of(const char *os) {
    if (strcmp(os, "macos") == 0) return "darwin";
    return os;   /* linux, windows and the BSDs already match */
}

static const char *goarch_of(BinArch a) {
    switch (a) {
        case BA_X86_64:  return "amd64";
        case BA_AARCH64: return "arm64";
        case BA_ARM:     return "arm";
        case BA_I386:    return "386";
        case BA_RISCV64: return "riscv64";
        case BA_PPC64:   return "ppc64";
        default:         return NULL;
    }
}

static const char *rid_arch_of(BinArch a) {
    switch (a) {
        case BA_X86_64:  return "x64";
        case BA_AARCH64: return "arm64";
        case BA_ARM:     return "arm";
        case BA_I386:    return "x86";
        default:         return NULL;
    }
}

/* .NET distinguishes musl from glibc in the identifier itself, and getting it
   wrong produces a binary that builds cleanly and then dies on startup against
   the wrong libc. The triple already says which one, so it decides. */
static const char *rid_os_of(const char *os, const char *triple) {
    if (strcmp(os, "macos")   == 0) return "osx";
    if (strcmp(os, "windows") == 0) return "win";
    if (strcmp(os, "linux")   == 0)
        return strstr(triple, "-musl") ? "linux-musl" : "linux";
    return NULL;
}

int buildvars_init(BuildVars *v, const char *triple, const char *artifact,
                   const char *name, const char *version, int jobs) {
    memset(v, 0, sizeof *v);

    v->triple   = triple;
    v->artifact = artifact;
    v->name     = name;
    v->version  = version;
    v->jobs     = jobs;

    BinFormat fmt;
    BinArch   arch;
    if (binfmt_from_triple(triple, &fmt, &arch) != 0) return -1;

    const char *os = os_of(triple);
    if (!os) return -1;

    snprintf(v->os,   sizeof v->os,   "%s", os);
    snprintf(v->arch, sizeof v->arch, "%s", binfmt_arch_name(arch));
    snprintf(v->goos, sizeof v->goos, "%s", goos_of(os));

    const char *ga = goarch_of(arch);
    if (ga) snprintf(v->goarch, sizeof v->goarch, "%s", ga);

    const char *ro = rid_os_of(os, triple);
    const char *ra = rid_arch_of(arch);
    if (ro && ra) snprintf(v->rid, sizeof v->rid, "%s-%s", ro, ra);

    return 0;
}

/* --------------------------------------------------------------------- */

static const char *lookup(const BuildVars *v, const char *key, size_t len,
                          char *scratch, size_t scratch_size) {
    #define MATCH(s) (len == strlen(s) && strncmp(key, s, len) == 0)

    if (MATCH("triple"))   return v->triple;
    if (MATCH("artifact")) return v->artifact;
    if (MATCH("name"))     return v->name;
    if (MATCH("version"))  return v->version;
    if (MATCH("os"))       return v->os;
    if (MATCH("arch"))     return v->arch;
    if (MATCH("goos"))     return v->goos[0]   ? v->goos   : NULL;
    if (MATCH("goarch"))   return v->goarch[0] ? v->goarch : NULL;
    if (MATCH("rid"))      return v->rid[0]    ? v->rid    : NULL;

    if (MATCH("jobs")) {
        snprintf(scratch, scratch_size, "%d", v->jobs);
        return scratch;
    }

    #undef MATCH
    return NULL;
}

int buildsys_expand(const char *tmpl, const BuildVars *v,
                    char *out, size_t out_size) {
    size_t n = 0;

    for (const char *p = tmpl; *p; ) {
        if (*p != '{') {
            if (n + 2 > out_size) return -1;
            out[n++] = *p++;
            continue;
        }

        const char *close = strchr(p, '}');
        if (!close) {
            fprintf(stderr, "atom: unclosed { in \"%s\"\n", tmpl);
            return -1;
        }

        char scratch[32];
        const char *value = lookup(v, p + 1, (size_t)(close - p - 1),
                                   scratch, sizeof scratch);
        if (!value) {
            fprintf(stderr, "atom: \"%s\" uses %.*s, which is not available "
                            "for this target\n",
                    tmpl, (int)(close - p + 1), p);
            return -1;
        }

        size_t len = strlen(value);
        if (n + len + 1 > out_size) return -1;
        memcpy(out + n, value, len);
        n += len;
        p = close + 1;
    }

    if (n + 1 > out_size) return -1;
    out[n] = '\0';
    return 0;
}
