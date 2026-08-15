#ifndef BUILD_H
#define BUILD_H

#include "manifest.h"

/* Options that come from the command line rather than the manifest. */
typedef struct {
    const char *source_root;  /* directory holding atom.toml               */
    const char *work_dir;     /* isolated per-target trees, default "build" */
    const char *dist_dir;     /* collected artifacts, default "dist"        */
    int         jobs;         /* concurrent targets, 0 = core count         */
    int         make_jobs;    /* -j passed to each build, 0 = auto          */
    int         verbose;      /* stream build output even on success        */
    const char *only;         /* build just this target id, or NULL         */
    int         assume_yes;   /* skip the confirmation before publishing    */
    int         dry_run;      /* show what would be published, do nothing   */
    int         no_cache;     /* rebuild even when the inputs are unchanged */
    int         keep_images;  /* leave images atom pulled on the machine     */
} BuildOpts;

/* Builds every target in the manifest. Returns the number that failed. */
int cmd_build(const Manifest *m, const BuildOpts *o);

/* Archives what `cmd_build` left in dist/, and writes SHA256SUMS. Returns the
   number of targets that could not be packaged. */
int cmd_package(const Manifest *m, const BuildOpts *o);

/* Prints the resolved target table without building anything. */
int cmd_targets(const Manifest *m);

#endif
