#include "toml.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void trim(char *s) {
    char *start = s;
    while (*start == ' ' || *start == '\t') start++;

    size_t len = strlen(start);
    char *end = start + len;
    while (end > start && (end[-1] == ' '  || end[-1] == '\t' ||
                           end[-1] == '\r' || end[-1] == '\n'))
        end--;
    *end = '\0';

    if (start != s) memmove(s, start, strlen(start) + 1);
}

static void strip_quotes(char *s) {
    size_t len = strlen(s);
    if (len >= 2 && ((s[0] == '"'  && s[len-1] == '"') ||
                     (s[0] == '\'' && s[len-1] == '\''))) {
        s[len-1] = '\0';
        memmove(s, s + 1, len - 1);
    }
}

/* Truncates at the first '#' that is not inside a quoted string, so
   `artifact = "app.exe"  # windows` yields `"app.exe"` rather than a value
   with the comment glued on. */
static void strip_comment(char *s) {
    char quote = 0;
    for (char *p = s; *p; p++) {
        if (quote) {
            if (*p == quote) quote = 0;
        } else if (*p == '"' || *p == '\'') {
            quote = *p;
        } else if (*p == '#') {
            *p = '\0';
            return;
        }
    }
}

int toml_parse(const char *path, TomlDoc *doc) {
    doc->count     = 0;
    doc->truncated = 0;

    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char line[TOML_MAX_VAL + TOML_MAX_KEY + 16];
    char section[TOML_MAX_KEY] = "";

    while (fgets(line, sizeof line, f)) {
        trim(line);

        if (line[0] == '\0' || line[0] == '#') continue;

        /* Section header: [build] or [target.linux-arm64] */
        if (line[0] == '[') {
            char *end = strchr(line, ']');
            if (!end) continue;
            *end = '\0';

            /* Tolerate the array-of-tables spelling by treating [[x]] as [x],
               so a manifest written either way still parses. */
            char *name = line + 1;
            if (*name == '[') name++;

            snprintf(section, sizeof section, "%s", name);
            trim(section);
            strip_quotes(section);
            continue;
        }

        char *eq = strchr(line, '=');
        if (!eq) continue;

        if (doc->count >= TOML_MAX_PAIRS) { doc->truncated = 1; break; }

        TomlPair *pair = &doc->pairs[doc->count++];

        *eq = '\0';
        snprintf(pair->key, sizeof pair->key, "%s", line);
        trim(pair->key);

        snprintf(pair->val, sizeof pair->val, "%s", eq + 1);
        strip_comment(pair->val);
        trim(pair->val);
        strip_quotes(pair->val);

        snprintf(pair->section, sizeof pair->section, "%s", section);
    }

    fclose(f);
    return 0;
}

const char *toml_get(const TomlDoc *doc, const char *section, const char *key) {
    for (int i = 0; i < doc->count; i++) {
        const TomlPair *p = &doc->pairs[i];
        if (strcmp(p->key, key) == 0 && strcmp(p->section, section) == 0)
            return p->val;
    }
    return NULL;
}

const char *toml_get_or(const TomlDoc *doc, const char *section,
                        const char *key, const char *fallback) {
    const char *val = toml_get(doc, section, key);
    return (val && *val) ? val : fallback;
}

int toml_get_bool(const TomlDoc *doc, const char *section,
                  const char *key, int fallback) {
    const char *v = toml_get(doc, section, key);
    if (!v || !*v) return fallback;

    if (strcmp(v, "true") == 0 || strcmp(v, "yes") == 0 ||
        strcmp(v, "on")   == 0 || strcmp(v, "1")   == 0) return 1;
    if (strcmp(v, "false") == 0 || strcmp(v, "no")  == 0 ||
        strcmp(v, "off")   == 0 || strcmp(v, "0")   == 0) return 0;

    return fallback;
}

const char *toml_section_next(const TomlDoc *doc, const char *prefix,
                              int *iter) {
    size_t plen = strlen(prefix);

    for (int i = *iter; i < doc->count; i++) {
        const char *s = doc->pairs[i].section;
        if (strncmp(s, prefix, plen) != 0) continue;

        /* Only the first pair of each section reports it. */
        int seen = 0;
        for (int j = 0; j < i; j++) {
            if (strcmp(doc->pairs[j].section, s) == 0) { seen = 1; break; }
        }
        if (seen) continue;

        *iter = i + 1;
        return s;
    }

    *iter = doc->count;
    return NULL;
}
