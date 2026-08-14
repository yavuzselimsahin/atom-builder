#ifndef BUILDSYS_H
#define BUILDSYS_H

#include <stddef.h>

/* What a build system needs from atom, and what atom needs from it.

   Every build system answers the same four questions — how do I invoke you for
   this target, where does the result land, how do I clean, and what belongs in
   the environment. Those answers are data, not code, so supporting one more is
   a table entry rather than a new branch. The machinery around it (isolated
   trees, parallelism, artifact verification, caching, packaging, publishing)
   never learns which one it is driving. */

typedef struct {
    const char *name;
    const char *command;     /* argv template                              */
    const char *output;      /* artifact path template, from the tree root */
    const char *clean;       /* run before every build, may be empty       */
    const char *env;         /* NAME=VALUE templates, space separated      */
    const char *cc_var;      /* compiler injection, for make-style systems */
    const char *strip_flag;  /* appended when [build] strip is on          */
} BuildSystem;

/* Looks a system up by name, or NULL if there is no such system. */
const BuildSystem *buildsys_find(const char *name);

/* Comma-separated list of known names, for error messages. */
const char *buildsys_names(void);

/* The values a template can refer to, derived once per target. */
typedef struct {
    const char *triple;
    const char *artifact;
    const char *name;
    const char *version;
    int         jobs;

    char os[16];      /* linux, macos, windows, freebsd            */
    char arch[16];    /* x86_64, aarch64, arm, i386, riscv64       */
    char goos[16];    /* Go's spelling: linux, darwin, windows     */
    char goarch[16];  /* Go's spelling: amd64, arm64, arm, 386     */
    char rid[16];     /* .NET's spelling: linux-x64, osx-arm64     */
} BuildVars;

/* Fills the derived fields from `triple`. Returns -1 when the triple names an
   architecture or system atom does not recognise. */
int buildvars_init(BuildVars *v, const char *triple, const char *artifact,
                   const char *name, const char *version, int jobs);

/* Substitutes {placeholders} in `tmpl`. Unknown placeholders are an error
   rather than being passed through, so a typo fails loudly instead of
   reaching a compiler as a literal brace. Returns 0 on success. */
int buildsys_expand(const char *tmpl, const BuildVars *v,
                    char *out, size_t out_size);

#endif
