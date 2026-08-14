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

#define ATOM_VERSION "0.1.0"

static void usage(void) {
    printf(
"atom %s — cross-platform builds for native projects\n"
"\n"
"usage: atom <command> [options]\n"
"\n"
"commands:\n"
"  init             write a starting atom.toml for this project\n"
"  build            build every target in the manifest\n"
"  verify           run each built binary, emulating where needed\n"
"  package          archive what build produced, and checksum it\n"
"  publish          upload the archives to the configured destinations\n"
"  release          build, verify, then package\n"
"  targets          list the targets without building\n"
"  version          print the version\n"
"\n"
"options:\n"
"  -f, --file <p>   manifest to read (default: atom.toml)\n"
"  -C <dir>         project directory (default: the manifest's directory)\n"
"  -t, --target <id>  build only this target\n"
"  -j <n>           targets to build at once (default: core count)\n"
"      --make-jobs <n>  -j passed to each build (default: 4)\n"
"  -v, --verbose    print build output even when a target succeeds\n"
"  -y, --yes        do not ask for confirmation before publishing\n"
"      --dry-run    show what publish would upload, without uploading\n"
"      --no-cache   rebuild even when nothing has changed\n"
"  -h, --help       this text\n"
"\n"
"publishing reads GITHUB_TOKEN (or GH_TOKEN) from the environment.\n",
    ATOM_VERSION);
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
    int no_cache = 0;

    /* A child that exits while atom is still writing to its stdin would
       otherwise kill atom outright; the write returns EPIPE instead. */
    signal(SIGPIPE, SIG_IGN);

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];

        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            usage();
            return 0;
        } else if (strcmp(a, "-f") == 0 || strcmp(a, "--file") == 0) {
            if (!need_arg(i, argc, a)) return 2;
            manifest = argv[++i];
        } else if (strcmp(a, "-C") == 0) {
            if (!need_arg(i, argc, a)) return 2;
            root = argv[++i];
        } else if (strcmp(a, "-t") == 0 || strcmp(a, "--target") == 0) {
            if (!need_arg(i, argc, a)) return 2;
            only = argv[++i];
        } else if (strcmp(a, "-j") == 0) {
            if (!need_arg(i, argc, a)) return 2;
            jobs = atoi(argv[++i]);
        } else if (strcmp(a, "--make-jobs") == 0) {
            if (!need_arg(i, argc, a)) return 2;
            make_jobs = atoi(argv[++i]);
        } else if (strcmp(a, "-v") == 0 || strcmp(a, "--verbose") == 0) {
            verbose = 1;
        } else if (strcmp(a, "-y") == 0 || strcmp(a, "--yes") == 0) {
            assume_yes = 1;
        } else if (strcmp(a, "--dry-run") == 0) {
            dry_run = 1;
        } else if (strcmp(a, "--no-cache") == 0) {
            no_cache = 1;
        } else if (a[0] == '-') {
            fprintf(stderr, "atom: unknown option %s\n", a);
            return 2;
        } else if (!command) {
            command = a;
        } else {
            fprintf(stderr, "atom: unexpected argument %s\n", a);
            return 2;
        }
    }

    if (!command) {
        usage();
        return 2;
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

    if (strcmp(command, "build")   != 0 &&
        strcmp(command, "verify")  != 0 &&
        strcmp(command, "package") != 0 &&
        strcmp(command, "publish") != 0 &&
        strcmp(command, "release") != 0) {
        fprintf(stderr, "atom: unknown command '%s'\n", command);
        usage();
        return 2;
    }

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
