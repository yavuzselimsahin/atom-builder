#include "../include/json.h"

#include <stdio.h>
#include <string.h>

static const char *skip_ws(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

/* Reads the string starting at `p`, which must point at the opening quote.
   Writes the unescaped value to `out` and returns the position just past the
   closing quote, or NULL if the string is malformed or does not fit. */
static const char *read_string(const char *p, char *out, size_t out_size) {
    if (*p != '"' || out_size == 0) return NULL;
    p++;

    size_t n = 0;
    while (*p && *p != '"') {
        char c;

        if (*p == '\\') {
            p++;
            switch (*p) {
                case '"':  c = '"';  break;
                case '\\': c = '\\'; break;
                case '/':  c = '/';  break;
                case 'b':  c = '\b'; break;
                case 'f':  c = '\f'; break;
                case 'n':  c = '\n'; break;
                case 'r':  c = '\r'; break;
                case 't':  c = '\t'; break;
                case 'u': {
                    /* Only the ASCII range is decoded; nothing atom reads from
                       GitHub carries anything else, and a placeholder is safer
                       than a half-built UTF-8 sequence. */
                    unsigned code = 0;
                    for (int i = 1; i <= 4; i++) {
                        char h = p[i];
                        if (h >= '0' && h <= '9')      code = code * 16 + (unsigned)(h - '0');
                        else if (h >= 'a' && h <= 'f') code = code * 16 + (unsigned)(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') code = code * 16 + (unsigned)(h - 'A' + 10);
                        else return NULL;
                    }
                    p += 4;
                    c = (code && code < 128) ? (char)code : '?';
                    break;
                }
                default: return NULL;
            }
            p++;
        } else {
            c = *p++;
        }

        if (n + 1 >= out_size) return NULL;
        out[n++] = c;
    }

    if (*p != '"') return NULL;
    out[n] = '\0';
    return p + 1;
}

int json_string(const char *json, const char *key, char *out, size_t out_size) {
    if (!json || !key) return -1;

    int depth = 0;
    const char *p = json;

    while (*p) {
        if (*p == '"') {
            char name[256];
            const char *after = read_string(p, name, sizeof name);

            if (!after) {
                /* A key too long for the buffer is simply not the one being
                   looked for; step over the quote and carry on. */
                p++;
                continue;
            }

            const char *q = skip_ws(after);
            if (*q == ':' && depth == 1 && strcmp(name, key) == 0) {
                q = skip_ws(q + 1);
                if (*q != '"') return -1;
                return read_string(q, out, out_size) ? 0 : -1;
            }

            p = after;
            continue;
        }

        if (*p == '{' || *p == '[')      depth++;
        else if (*p == '}' || *p == ']') depth--;
        p++;
    }

    return -1;
}

int json_escape(const char *s, char *out, size_t out_size) {
    size_t n = 0;

    for (; *s; s++) {
        const char *rep = NULL;
        char        esc[8];

        switch (*s) {
            case '"':  rep = "\\\""; break;
            case '\\': rep = "\\\\"; break;
            case '\n': rep = "\\n";  break;
            case '\r': rep = "\\r";  break;
            case '\t': rep = "\\t";  break;
            default:
                if ((unsigned char)*s < 0x20) {
                    snprintf(esc, sizeof esc, "\\u%04x", (unsigned char)*s);
                    rep = esc;
                }
                break;
        }

        if (rep) {
            size_t len = strlen(rep);
            if (n + len + 1 > out_size) return -1;
            memcpy(out + n, rep, len);
            n += len;
        } else {
            if (n + 2 > out_size) return -1;
            out[n++] = *s;
        }
    }

    if (n + 1 > out_size) return -1;
    out[n] = '\0';
    return 0;
}
