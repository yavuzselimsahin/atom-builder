#ifndef CACHE_H
#define CACHE_H

#include "manifest.h"
#include "build.h"
#include "hash.h"

/* Skipping a rebuild is only safe if the key covers everything that could
   change the output. That means the content of every source file, the exact
   command that would run, and the toolchain producing it — not timestamps,
   which say nothing about whether a file's bytes actually differ.

   Excluded from the walk, and so from the key: the work and dist trees, and
   .git. Those are outputs and metadata, not inputs. */

/* Computes the key for one target. `command` is the build argv, joined, so
   that a changed flag invalidates the entry. Returns 0 on success. */
int cache_key(const Manifest *m, const Target *t, const BuildOpts *o,
              char *const command[], char *const env[], const char *toolchain,
              char out_hex[SHA256_HEX_LEN]);

/* Copies a cached artifact to `dest` when the stored key matches. Returns 0 on
   a hit, -1 on a miss. */
int cache_lookup(const BuildOpts *o, const Target *t, const char *key,
                 const char *artifact, const char *dest);

/* Records `artifact` under `key` for this target. Failures are not fatal — a
   cache that cannot be written only costs time. */
void cache_store(const BuildOpts *o, const Target *t, const char *key,
                 const char *artifact, const char *source);

/* Identifies the toolchain, e.g. "zig 0.16.0". Cached after the first call. */
const char *toolchain_id(void);

#endif
