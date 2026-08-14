#ifndef PUBLISH_H
#define PUBLISH_H

#include "manifest.h"
#include "build.h"

#define MAX_ASSETS (MAX_TARGETS + 1)   /* every archive, plus SHA256SUMS */

/* The files a publish would upload, gathered from dist/. */
typedef struct {
    char paths[MAX_ASSETS][1024];  /* on disk                    */
    char names[MAX_ASSETS][256];   /* as published               */
    long long sizes[MAX_ASSETS];
    int  count;
} AssetSet;

/* Collects the archives named by the manifest. Returns -1 and explains itself
   if any is missing, since a partial upload looks like a complete release. */
int assets_collect(const Manifest *m, const BuildOpts *o, AssetSet *set);

/* Lists what is about to be uploaded and asks for confirmation. Returns
   non-zero to proceed. */
int publish_confirm(const AssetSet *set, const char *destination);

/* Mirrors the assets to a host over rsync, or to a local path when
   [publish.ssh] host is empty. */
int publish_ssh(const Manifest *m, const BuildOpts *o, const AssetSet *set);

/* Creates or reuses a GitHub release for the manifest's tag and attaches the
   assets to it. */
int publish_github(const Manifest *m, const BuildOpts *o, const AssetSet *set);

/* Runs whichever destinations the manifest configures. */
int cmd_publish(const Manifest *m, const BuildOpts *o);

#endif
