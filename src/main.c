#include "../include/manifest.h"
#include "../include/build.h"
#include "../include/publish.h"
#include "../include/verify.h"
#include "../include/init.h"
#include "../include/util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#define ATOM_VERSION "1.0.0"
#define DOC_URL "https://yavuzselimsahin.github.io/atom-builder/"

/* Options come in groups, and each command accepts some of them. Keeping that
   as a bitmask rather than a comment means `atom package --no-cache` can be
   rejected with a reason instead of silently ignored, and means the per-command
   help can list exactly the flags that apply. */
#define OPT_MANIFEST (1u << 0)   /* -f, -C                */
#define OPT_TARGET   (1u << 1)   /* -t                    */
#define OPT_JOBS     (1u << 2)   /* -j, --make-jobs       */
#define OPT_VERBOSE  (1u << 3)   /* -v                    */
#define OPT_CONFIRM  (1u << 4)   /* -y, --dry-run         */
#define OPT_CACHE    (1u << 5)   /* --no-cache            */

#define OPT_GROUPS   6

typedef struct {
    const char *name;
    const char *summary;   /* one line, for the command list          */
    const char *usage;     /* the usage line, minus the program name  */
    const char *detail;    /* shown by `atom <command> --help`        */
    unsigned    opts;
} Command;

static const Command COMMANDS[] = {
{
    "init", "write a starting atom.toml for this project",
    "atom init [-C <dir>]",
    "Writes a starting atom.toml for the project in the current directory.\n"
    "\n"
    "It reads the directory rather than printing a blank template: a\n"
    "Cargo.toml, go.mod, build.zig, .csproj or Makefile decides the build\n"
    "system, and the triples it writes are spelled the way that system\n"
    "expects. A Makefile is scanned for the name of the binary it produces.\n"
    "\n"
    "An existing atom.toml is never overwritten.",
    OPT_MANIFEST
},
{
    "targets", "list the targets without building",
    "atom targets [-f <path>] [-C <dir>]",
    "Prints the resolved target table — id, triple, the format and\n"
    "architecture each one should produce, and the artifact name — and\n"
    "builds nothing.\n"
    "\n"
    "The quickest way to check that a manifest parses and that each target\n"
    "resolves to what you expected.",
    OPT_MANIFEST
},
{
    "build", "build every target in the manifest",
    "atom build [-t <id>] [-j <n>] [--no-cache] [-v]",
    "Builds every target, in parallel.\n"
    "\n"
    "Each target gets its own clone of the source tree, cleaned before the\n"
    "build so stale object files cannot link into it. Every finished artifact\n"
    "is identified by reading its own header and compared against what the\n"
    "target asked for, so a build that exits 0 with the wrong output still\n"
    "fails. Targets whose inputs have not changed are served from the cache.",
    OPT_MANIFEST | OPT_TARGET | OPT_JOBS | OPT_VERBOSE | OPT_CACHE
},
{
    "verify", "run each built binary, emulating where needed",
    "atom verify [-t <id>] [-v]",
    "Runs each built binary and reports whether it starts, using emulation\n"
    "where the host cannot execute the target directly.\n"
    "\n"
    "A target with no available runner is skipped rather than failed, unless\n"
    "it set `verify = true` in the manifest. Skips alone do not fail the\n"
    "command; -v explains each one.",
    OPT_MANIFEST | OPT_TARGET | OPT_VERBOSE
},
{
    "package", "archive what build produced, and checksum it",
    "atom package [-t <id>]",
    "Archives what `atom build` left in dist/ and writes SHA256SUMS.\n"
    "\n"
    "Unix targets get .tar.gz and Windows targets .zip, each holding one\n"
    "<name>-<version>-<target>/ directory. A target that has not been built\n"
    "yet is an error.\n"
    "\n"
    "With -t, SHA256SUMS is not written: it describes a whole release.",
    OPT_MANIFEST | OPT_TARGET
},
{
    "publish", "upload the archives to the configured destinations",
    "atom publish [-t <id>] [--dry-run] [-y]",
    "Uploads the archives and SHA256SUMS to every destination the manifest\n"
    "configures, listing what will be sent and asking first.\n"
    "\n"
    "--dry-run shows the plan and contacts nothing. -y skips the question,\n"
    "for scripts.\n"
    "\n"
    "GitHub publishing reads GITHUB_TOKEN, falling back to GH_TOKEN. The\n"
    "token is handed to curl on stdin, so it never appears in the process\n"
    "arguments and is never written to disk.",
    OPT_MANIFEST | OPT_TARGET | OPT_CONFIRM | OPT_VERBOSE
},
{
    "release", "build, verify, then package",
    "atom release [-t <id>] [-j <n>] [--no-cache] [-v]",
    "Runs build, then verify, then package — each conditional on the previous\n"
    "stage fully succeeding, so a partial set of archives is never written.\n"
    "\n"
    "Publishing stays separate on purpose: anything that leaves your machine\n"
    "should be a deliberate act.",
    OPT_MANIFEST | OPT_TARGET | OPT_JOBS | OPT_VERBOSE | OPT_CACHE
},
{
    "version", "print the version",
    "atom version",
    "Prints the version and exits.",
    0
},
};

#define COMMAND_COUNT ((int)(sizeof COMMANDS / sizeof COMMANDS[0]))

static const Command *find_command(const char *name) {
    for (int i = 0; i < COMMAND_COUNT; i++)
        if (strcmp(COMMANDS[i].name, name) == 0) return &COMMANDS[i];
    return NULL;
}

/* One row per option group, so the overview and the per-command help are
   printed from the same source rather than drifting apart. */
static void print_options(unsigned mask) {
    if (mask & OPT_MANIFEST) {
        printf("  -f, --file <path>    manifest to read (default: atom.toml)\n");
        printf("  -C <dir>             project directory "
               "(default: the manifest's)\n");
    }
    if (mask & OPT_TARGET)
        printf("  -t, --target <id>    act on this target only\n");
    if (mask & OPT_JOBS) {
        printf("  -j <n>               targets at once (default: core count)\n");
        printf("      --make-jobs <n>  -j passed to each build (default: 4)\n");
    }
    if (mask & OPT_CACHE)
        printf("      --no-cache       rebuild even when nothing has changed\n");
    if (mask & OPT_CONFIRM) {
        printf("      --dry-run        show what would be uploaded, "
               "upload nothing\n");
        printf("  -y, --yes            do not ask for confirmation\n");
    }
    if (mask & OPT_VERBOSE)
        printf("  -v, --verbose        show the underlying tool's own output\n");

    printf("  -h, --help           this text\n");
}

static void usage(void) {
    printf("atom %s — cross-platform builds for native projects\n"
           "\n"
           "usage: atom <command> [options]\n"
           "\n"
           "commands:\n", ATOM_VERSION);

    for (int i = 0; i < COMMAND_COUNT; i++)
        printf("  %-9s %s\n", COMMANDS[i].name, COMMANDS[i].summary);

    printf("\n"
           "Run `atom <command> --help` for a command's own options.\n"
           "\n"
           "  %s\n", DOC_URL);
}

static void command_help(const Command *c) {
    printf("usage: %s\n\n%s\n\noptions:\n", c->usage, c->detail);
    print_options(c->opts);
}

/* Returns the directory part of `path`, or "." when there is none. The result
   is written into buf so the caller owns it. */
static const char *dirname_of(const char *path, char *buf, size_t size) {
    const char *slash = strrchr(path, '/');
    if (!slash) return ".";

    size_t n = (size_t)(slash - path);
    if (n == 0) n = 1;                       /* "/atom.toml" -> "/" */
    if (n >= size) n = size - 1;

    memcpy(buf, path, n);
    buf[n] = '\0';
    return buf;
}

static int need_arg(int i, int argc, const char *flag) {
    if (i + 1 < argc) return 1;
    fprintf(stderr, "atom: %s needs a value\n", flag);
    return 0;
}

int main(int argc, char **argv) {
    const char *command  = NULL;
    const char *manifest = "atom.toml";
    const char *root     = NULL;
    const char *only     = NULL;
    int jobs = 0, make_jobs = 0, verbose = 0, assume_yes = 0, dry_run = 0;
    int no_cache = 0, want_help = 0;

    /* Which option groups were used, and the flag that first used each, so a
       flag the command does not accept can be named back to the user. */
    unsigned    used = 0;
    const char *used_by[OPT_GROUPS];
    memset(used_by, 0, sizeof used_by);

    /* A child that exits while atom is still writing to its stdin would
       otherwise kill atom outright; the write returns EPIPE instead. */
    signal(SIGPIPE, SIG_IGN);

    #define MARK(bit, flag) do {                                    \
        used |= (bit);                                              \
        for (int b = 0; b < OPT_GROUPS; b++)                        \
            if ((bit) == (1u << b) && !used_by[b]) used_by[b] = (flag); \
    } while (0)

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];

        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            want_help = 1;
        } else if (strcmp(a, "-f") == 0 || strcmp(a, "--file") == 0) {
            if (!need_arg(i, argc, a)) return 2;
            manifest = argv[++i];
            MARK(OPT_MANIFEST, a);
        } else if (strcmp(a, "-C") == 0) {
            if (!need_arg(i, argc, a)) return 2;
            root = argv[++i];
            MARK(OPT_MANIFEST, a);
        } else if (strcmp(a, "-t") == 0 || strcmp(a, "--target") == 0) {
            if (!need_arg(i, argc, a)) return 2;
            only = argv[++i];
            MARK(OPT_TARGET, a);
        } else if (strcmp(a, "-j") == 0) {
            if (!need_arg(i, argc, a)) return 2;
            jobs = atoi(argv[++i]);
            MARK(OPT_JOBS, a);
        } else if (strcmp(a, "--make-jobs") == 0) {
            if (!need_arg(i, argc, a)) return 2;
            make_jobs = atoi(argv[++i]);
            MARK(OPT_JOBS, a);
        } else if (strcmp(a, "-v") == 0 || strcmp(a, "--verbose") == 0) {
            verbose = 1;
            MARK(OPT_VERBOSE, a);
        } else if (strcmp(a, "-y") == 0 || strcmp(a, "--yes") == 0) {
            assume_yes = 1;
            MARK(OPT_CONFIRM, a);
        } else if (strcmp(a, "--dry-run") == 0) {
            dry_run = 1;
            MARK(OPT_CONFIRM, a);
        } else if (strcmp(a, "--no-cache") == 0) {
            no_cache = 1;
            MARK(OPT_CACHE, a);
        } else if (a[0] == '-') {
            fprintf(stderr, "atom: unknown option %s\n", a);
            return 2;
        } else if (!command) {
            command = a;
        } else if (strcmp(command, "help") == 0) {
            /* `atom help build`, the spelling git taught everyone. */
            const Command *c = find_command(a);
            if (!c) {
                fprintf(stderr, "atom: no such command '%s'\n", a);
                return 2;
            }
            command_help(c);
            return 0;
        } else {
            fprintf(stderr, "atom: unexpected argument %s\n", a);
            return 2;
        }
    }

    #undef MARK

    if (command && strcmp(command, "help") == 0) {
        usage();
        return 0;
    }

    if (!command) {
        usage();
        return want_help ? 0 : 2;
    }

    const Command *cmd = find_command(command);
    if (!cmd) {
        fprintf(stderr, "atom: no such command '%s'\n\n", command);
        usage();
        return 2;
    }

    if (want_help) {
        command_help(cmd);
        return 0;
    }

    /* An option the command does not take is a mistake worth stopping for:
       silently ignoring it leaves the user believing something happened. */
    unsigned wrong = used & ~cmd->opts;
    if (wrong) {
        for (int b = 0; b < OPT_GROUPS; b++) {
            if (!(wrong & (1u << b)) || !used_by[b]) continue;
            fprintf(stderr, "atom: %s does not take %s\n",
                    cmd->name, used_by[b]);
            fprintf(stderr, "      try `atom %s --help`\n", cmd->name);
            return 2;
        }
    }

    if (strcmp(command, "version") == 0) {
        printf("atom %s\n", ATOM_VERSION);
        return 0;
    }

    /* A manifest given as a path implies its own directory as the project
       root, so `atom build -f ../other/atom.toml` does the obvious thing. */
    char rootbuf[1024];
    if (!root) root = dirname_of(manifest, rootbuf, sizeof rootbuf);

    /* `init` is the one command that runs without a manifest — it is what
       produces one. */
    if (strcmp(command, "init") == 0) return cmd_init(root);

    Manifest m;
    if (manifest_load(manifest, &m) != 0) return 1;

    if (strcmp(command, "targets") == 0) return cmd_targets(&m);

    char work[1024], dist[1024];
    snprintf(work, sizeof work, "%s/build", root);
    snprintf(dist, sizeof dist, "%s/dist",  root);

    BuildOpts opts = {
        .source_root = root,
        .work_dir    = work,
        .dist_dir    = dist,
        .jobs        = jobs > 0 ? jobs : cpu_count(),
        .make_jobs   = make_jobs,
        .verbose     = verbose,
        .only        = only,
        .assume_yes  = assume_yes,
        .dry_run     = dry_run,
        .no_cache    = no_cache,
    };

    if (strcmp(command, "verify") == 0)
        return cmd_verify(&m, &opts) == 0 ? 0 : 1;

    if (strcmp(command, "package") == 0)
        return cmd_package(&m, &opts) == 0 ? 0 : 1;

    if (strcmp(command, "publish") == 0)
        return cmd_publish(&m, &opts) == 0 ? 0 : 1;

    if (cmd_build(&m, &opts) != 0) return 1;

    /* `release` only packages a build that fully succeeded — a partial set of
       archives is worse than none, because it looks like a complete release.
       Verification runs in between, so nothing that fails to start is ever
       archived. */
    if (strcmp(command, "release") == 0) {
        putchar('\n');
        if (cmd_verify(&m, &opts) != 0) return 1;
        putchar('\n');
        return cmd_package(&m, &opts) == 0 ? 0 : 1;
    }
    return 0;
}
