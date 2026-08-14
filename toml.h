#ifndef TOML_H
#define TOML_H

/* A flat, section-scoped TOML reader. Derived from the one in atomik-ssg,
   with a larger pair budget, inline-comment handling and boolean lookup.

   It deliberately does not implement arrays of tables. Repeated entities are
   expressed as dotted sections instead — `[target.linux-arm64]` parses as a
   section literally named "target.linux-arm64", which section_next() can
   enumerate by prefix. That keeps the parser small and gives every target a
   name rather than an index. */

#define TOML_MAX_KEY   128
#define TOML_MAX_VAL   512
#define TOML_MAX_PAIRS 256

typedef struct {
    char key[TOML_MAX_KEY];
    char val[TOML_MAX_VAL];
    char section[TOML_MAX_KEY];
} TomlPair;

typedef struct {
    TomlPair pairs[TOML_MAX_PAIRS];
    int count;
    int truncated;   /* set when the file held more pairs than fit */
} TomlDoc;

int toml_parse(const char *path, TomlDoc *doc);

const char *toml_get(const TomlDoc *doc, const char *section, const char *key);
const char *toml_get_or(const TomlDoc *doc, const char *section,
                        const char *key, const char *fallback);
/* Accepts true/false, yes/no, on/off, 1/0. Returns fallback if absent or
   unparsable. */
int toml_get_bool(const TomlDoc *doc, const char *section,
                  const char *key, int fallback);

/* Enumerates distinct sections beginning with `prefix`. Pass *iter = 0 to
   start; returns the section name and advances *iter, or NULL when done. */
const char *toml_section_next(const TomlDoc *doc, const char *prefix,
                              int *iter);

#endif
