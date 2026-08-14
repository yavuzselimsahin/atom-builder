#ifndef JSON_H
#define JSON_H

#include <stddef.h>

/* Just enough JSON to read a reply, not a general parser.

   atom consumes exactly one kind of document — GitHub's API responses — and
   needs one thing from it: the value of a key at the top level. Depth is
   tracked so that a nested object carrying the same key name (a release reply
   holds an `author` with its own `url` and `id`) cannot be mistaken for the
   one being asked for. */

/* Copies the string value of a top-level `key` into `out`. Returns 0 on
   success, -1 if the key is absent or its value is not a string. */
int json_string(const char *json, const char *key, char *out, size_t out_size);

/* Escapes `s` as a JSON string body, without the surrounding quotes.
   Returns -1 if the result would not fit. */
int json_escape(const char *s, char *out, size_t out_size);

#endif
