#ifndef VERIFY_H
#define VERIFY_H

#include "manifest.h"
#include "build.h"

/* How a built binary can be executed on this machine.

   Cross-compilation's one real weakness is that it produces a binary nobody
   has run. `atom build` proves the right thing was *compiled*, by reading the
   artifact's header. This proves it *starts*. */
typedef enum {
    RUN_NONE = 0,  /* no way to execute this target here */
    RUN_HOST,      /* runs directly: same OS, and an arch the host executes */
    RUN_QEMU,      /* qemu-<arch> on a Linux host                          */
    RUN_DOCKER,    /* a Linux container, which supplies qemu itself        */
    RUN_WINE
} RunnerKind;

const char *runner_name(RunnerKind kind);

/* Chooses a runner for `triple` on this host, or RUN_NONE with a reason
   written to `why`. */
RunnerKind runner_select(const Manifest *m, const char *triple,
                         char *why, size_t why_size);

/* Runs every built target and reports whether each one starts. Targets with no
   available runner are skipped, unless the target set `verify = true`, which
   declares that it must be checked. */
int cmd_verify(const Manifest *m, const BuildOpts *o);

#endif
