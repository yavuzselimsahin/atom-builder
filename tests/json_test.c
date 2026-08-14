/* Unit tests for the JSON reader. It is the one piece of parsing that faces
   somebody else's data, so it is checked against the shape GitHub actually
   returns rather than only against tidy examples. */

#include "../include/json.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(const char *name, int cond) {
    if (cond) {
        printf("  ok   %s\n", name);
    } else {
        printf("  FAIL %s\n", name);
        failures++;
    }
}

static void check_str(const char *name, const char *got, const char *want) {
    if (strcmp(got, want) == 0) {
        printf("  ok   %s\n", name);
    } else {
        printf("  FAIL %s\n       got:  %s\n       want: %s\n", name, got, want);
        failures++;
    }
}

int main(void) {
    char out[512];

    printf("json\n");

    /* A release reply, trimmed but with the nesting that matters: `author`
       carries its own `url` and `id`, and appears before the keys being read.
       A parser that ignored depth would return the author's values. */
    const char *release =
        "{\"url\":\"https://api.github.com/repos/o/r/releases/1\","
        "\"assets_url\":\"https://api.github.com/repos/o/r/releases/1/assets\","
        "\"author\":{\"login\":\"someone\",\"id\":99,"
        "\"url\":\"https://api.github.com/users/someone\","
        "\"upload_url\":\"https://example.invalid/decoy\"},"
        "\"upload_url\":\"https://uploads.github.com/repos/o/r/releases/1/"
        "assets{?name,label}\","
        "\"tag_name\":\"v0.3.0\",\"assets\":[]}";

    check("finds a top-level string",
          json_string(release, "tag_name", out, sizeof out) == 0);
    check_str("reads the right value", out, "v0.3.0");

    check("finds upload_url",
          json_string(release, "upload_url", out, sizeof out) == 0);
    check_str("skips the nested object's upload_url", out,
              "https://uploads.github.com/repos/o/r/releases/1/assets{?name,label}");

    check("finds the top-level url",
          json_string(release, "url", out, sizeof out) == 0);
    check_str("does not return the author's url", out,
              "https://api.github.com/repos/o/r/releases/1");

    check("a key inside a nested object is not top level",
          json_string(release, "login", out, sizeof out) != 0);

    check("a missing key is reported",
          json_string(release, "nope", out, sizeof out) != 0);

    check("a non-string value is refused",
          json_string("{\"n\":42}", "n", out, sizeof out) != 0);

    /* Escapes */
    check("decodes escapes",
          json_string("{\"k\":\"a\\\"b\\\\c\\nd\\tf\"}", "k",
                      out, sizeof out) == 0);
    check_str("escape decoding is correct", out, "a\"b\\c\nd\tf");

    check("decodes \\u for ascii",
          json_string("{\"k\":\"a\\u0062c\"}", "k", out, sizeof out) == 0);
    check_str("\\u decoding is correct", out, "abc");

    check("a key inside an array element is not top level",
          json_string("{\"a\":[{\"k\":\"x\"}]}", "k", out, sizeof out) != 0);

    check("a value containing braces does not confuse depth",
          json_string("{\"a\":\"{not:an,object}\",\"b\":\"ok\"}", "b",
                      out, sizeof out) == 0);
    check_str("depth survives braces in strings", out, "ok");

    check("a value containing a quote does not confuse depth",
          json_string("{\"a\":\"say \\\"hi\\\"\",\"b\":\"ok\"}", "b",
                      out, sizeof out) == 0);
    check_str("depth survives escaped quotes", out, "ok");

    check("an empty document has no keys",
          json_string("{}", "k", out, sizeof out) != 0);

    check("truncation is refused rather than silently cut",
          json_string("{\"k\":\"abcdefghij\"}", "k", out, 4) != 0);

    /* Escaping the other way */
    check("escapes a quote", json_escape("a\"b", out, sizeof out) == 0);
    check_str("quote escaping is correct", out, "a\\\"b");

    check("escapes a backslash", json_escape("a\\b", out, sizeof out) == 0);
    check_str("backslash escaping is correct", out, "a\\\\b");

    check("escapes a control character",
          json_escape("a\x01" "b", out, sizeof out) == 0);
    check_str("control escaping is correct", out, "a\\u0001b");

    check("escaping refuses to overflow",
          json_escape("aaaaaaaaaa", out, 4) != 0);

    printf("\n%s\n", failures ? "json: FAILED" : "json: ok");
    return failures ? 1 : 0;
}
