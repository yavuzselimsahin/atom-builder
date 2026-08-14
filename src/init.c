#include "../include/init.h"
#include "../include/util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <limits.h>
#include <sys/stat.h>

/* Cargo spells its triples differently from zig, and a template that names
   them the other way round fails on the first run. Whichever system is
   detected picks its own spelling. */
typedef struct {
    const char *id;
    const char *zig_triple;
    const char *rust_triple;
} DefaultTarget;

static const DefaultTarget TARGETS[] = {
    { "linux-x86_64",   "x86_64-linux-musl",  "x86_64-unknown-linux-musl"  },
    { "linux-arm64",    "aarch64-linux-musl", "aarch64-unknown-linux-musl" },
    { "macos-arm64",    "aarch64-macos",      "aarch64-apple-darwin"       },
    { "macos-x86_64",   "x86_64-macos",       "x86_64-apple-darwin"        },
    { "windows-x86_64", "x86_64-windows-gnu", "x86_64-pc-windows-gnu"      },
};

#define TARGET_COUNT ((int)(sizeof TARGETS / sizeof TARGETS[0]))

static int exists(const char *dir, const char *name) {
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, name);

    struct stat st;
    return stat(path, &st) == 0;
}

/* True when `dir` holds a file ending in `suffix`. */
static int has_suffix(const char *dir, const char *suffix) {
    DIR *d = opendir(dir);
    if (!d) return 0;

    size_t slen = strlen(suffix);
    int    found = 0;
    struct dirent *e;

    while (!found && (e = readdir(d))) {
        size_t nlen = strlen(e->d_name);
        if (nlen > slen && strcmp(e->d_name + nlen - slen, suffix) == 0)
            found = 1;
    }

    closedir(d);
    return found;
}

/* Most specific first: a Rust or Go project may also carry a Makefile that
   only wraps the real build, so the language's own marker wins. */
static const char *detect_system(const char *dir) {
    if (exists(dir, "Cargo.toml"))  return "cargo";
    if (exists(dir, "go.mod"))      return "go";
    if (exists(dir, "build.zig"))   return "zig";
    if (has_suffix(dir, ".csproj") ||
        has_suffix(dir, ".fsproj") ||
        has_suffix(dir, ".sln"))    return "dotnet";
    if (exists(dir, "Makefile") ||
        exists(dir, "makefile") ||
        exists(dir, "GNUmakefile")) return "make";
    return NULL;
}

/* The binary a Makefile produces is usually named in a variable near the top.
   Guessing it from the directory name is right often enough to be tempting and
   wrong often enough to matter, and a wrong artifact name fails every target
   at once — so the Makefile is asked first.

   Only literal values are accepted: a name built from other variables cannot
   be resolved without evaluating the Makefile, and half-expanding it would be
   worse than not trying. Returns 0 when nothing usable was found. */
static int makefile_artifact(const char *dir, char *out, size_t out_size) {
    static const char *names[] = { "BIN", "TARGET", "PROG", "EXE", "NAME" };
    static const char *files[] = { "Makefile", "makefile", "GNUmakefile" };

    for (size_t fi = 0; fi < sizeof files / sizeof files[0]; fi++) {
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", dir, files[fi]);

        FILE *f = fopen(path, "r");
        if (!f) continue;

        char line[512];
        while (fgets(line, sizeof line, f)) {
            char *p = line;
            while (*p == ' ' || *p == '\t') p++;

            for (size_t ni = 0; ni < sizeof names / sizeof names[0]; ni++) {
                size_t len = strlen(names[ni]);
                if (strncmp(p, names[ni], len) != 0) continue;

                char *q = p + len;
                while (*q == ' ' || *q == '\t') q++;

                /* :=  ?=  +=  = */
                if (*q == ':' || *q == '?' || *q == '+') q++;
                if (*q != '=') continue;
                q++;

                while (*q == ' ' || *q == '\t') q++;

                char *end = q;
                while (*end && *end != ' ' && *end != '\t' &&
                       *end != '\r' && *end != '\n' && *end != '#')
                    end++;
                *end = '\0';

                if (!*q || strstr(q, "$(") || strstr(q, "${")) continue;

                snprintf(out, out_size, "%s", q);
                fclose(f);
                return 1;
            }
        }
        fclose(f);
    }
    return 0;
}

/* The project's name is almost always its directory's name. */
static void guess_name(const char *dir, char *out, size_t out_size) {
    char resolved[1024];
    if (!realpath(dir, resolved)) {
        snprintf(out, out_size, "myproject");
        return;
    }

    const char *slash = strrchr(resolved, '/');
    const char *base  = slash ? slash + 1 : resolved;

    if (!*base) base = "myproject";
    snprintf(out, out_size, "%s", base);
}

int cmd_init(const char *dir) {
    char path[1024];
    snprintf(path, sizeof path, "%s/atom.toml", dir);

    struct stat st;
    if (stat(path, &st) == 0) {
        fprintf(stderr, "atom: %s already exists — nothing was changed\n",
                path);
        return 1;
    }

    const char *system = detect_system(dir);
    int         guessed = system != NULL;
    if (!system) system = "custom";

    char name[128];
    guess_name(dir, name, sizeof name);

    /* The project's name and the file it produces are different things: the
       first names the release, the second is what the build actually writes. */
    char artifact[128];
    int  artifact_known = 0;

    if (strcmp(system, "make") == 0)
        artifact_known = makefile_artifact(dir, artifact, sizeof artifact);
    if (!artifact_known)
        snprintf(artifact, sizeof artifact, "%s", name);

    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "atom: %s: %s\n", path, strerror(errno));
        return 1;
    }

    fprintf(f,
        "[project]\n"
        "name    = \"%s\"\n"
        "version = \"0.1.0\"\n"
        "\n"
        "[build]\n"
        "system   = \"%s\"\n"
        "artifact = \"%s\"%s\n",
        name, system, artifact,
        artifact_known ? "" : "    # check this: the file your build produces");

    if (strcmp(system, "custom") == 0) {
        fprintf(f,
            "\n"
            "# No build system was recognised, so atom needs to be told how to\n"
            "# build and where the result lands. Placeholders available here:\n"
            "# {triple} {artifact} {name} {version} {jobs} {os} {arch}\n"
            "# {goos} {goarch} {rid}\n"
            "command  = \"./build.sh {triple}\"\n"
            "output   = \"out/{triple}/%s\"\n",
            artifact);
    }

    fprintf(f,
        "strip    = true\n"
        "\n"
        "[verify]\n"
        "# Runs each built binary to prove it starts. Checking the output\n"
        "# matters: plenty of programs exit 0 while doing nothing useful.\n"
        "# args   = \"--version\"\n"
        "# expect = \"%s\"\n"
        "\n"
        "[package]\n"
        "include  = \"README.md LICENSE\"\n"
        "checksum = true\n"
        "\n"
        "# Uncomment a destination to publish to.\n"
        "#\n"
        "# [publish.github]\n"
        "# repo = \"owner/%s\"\n"
        "#\n"
        "# [publish.ssh]\n"
        "# host = \"user@vps\"       # omit for a local path\n"
        "# path = \"/var/www/dl/%s\"\n",
        name, name, name);

    int is_rust = strcmp(system, "cargo") == 0;

    for (int i = 0; i < TARGET_COUNT; i++) {
        const DefaultTarget *t = &TARGETS[i];
        int windows = strstr(t->id, "windows") != NULL;

        fprintf(f, "\n[target.%s]\ntriple = \"%s\"\n",
                t->id, is_rust ? t->rust_triple : t->zig_triple);

        if (windows) {
            fprintf(f, "artifact = \"%s.exe\"\n", artifact);

            /* Every other toolchain names the Windows binary itself. A
               Makefile does not: it decides inside the file, usually behind an
               `ifeq ($(OS),Windows_NT)` check that never fires while
               cross-compiling. Passing the variable is what makes the target
               work on the first run rather than after reading a comment. */
            if (strcmp(system, "make") == 0)
                fprintf(f, "make = \"BIN=%s.exe\"    "
                           "# your Makefile may spell this differently\n",
                        artifact);
        }

        /* One target is marked so that a machine which cannot run anything
           still fails loudly rather than skipping every check silently. */
        if (strcmp(t->id, "linux-arm64") == 0)
            fprintf(f, "verify = true    # this one must be verified, "
                       "not skipped\n");
    }

    if (strcmp(system, "make") == 0)
        fprintf(f,
            "\n# A Windows build that uses sockets also needs winsock, which"
            " the\n"
            "# Makefile only links when it thinks it is on Windows:\n"
            "#   make = \"BIN=%s.exe LIBS=-lws2_32\"\n", artifact);

    if (fclose(f) != 0) {
        fprintf(stderr, "atom: could not finish writing %s\n", path);
        return 1;
    }

    printf("wrote %s\n\n", path);

    if (guessed)
        printf("  detected %s\n", system);
    else
        printf("  no build system recognised — filled in `custom`, which "
               "needs\n  command and output set before it will run\n");

    printf("  %d targets, project name \"%s\"\n", TARGET_COUNT, name);
    if (artifact_known)
        printf("  artifact \"%s\", read from the Makefile\n\n", artifact);
    else
        printf("  artifact guessed as \"%s\" — check it\n\n", artifact);
    printf("Next: check it over, then run `atom targets`.\n");

    return 0;
}
