#include "../include/publish.h"
#include "../include/exec.h"
#include "../include/json.h"
#include "../include/util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GH_API "https://api.github.com"

/* The token never reaches argv. curl reads it from a config file on stdin, so
   it is invisible to `ps` and never written to disk. */
static int build_curl_config(const char *token, StrBuf *cfg) {
    /* A quote or newline in the token would break out of the config's own
       quoting, so a malformed token is refused rather than escaped. */
    for (const char *p = token; *p; p++) {
        if (*p == '"' || *p == '\\' || *p == '\n' || *p == '\r') {
            fprintf(stderr, "atom: the GitHub token contains characters that "
                            "cannot be passed safely\n");
            return -1;
        }
    }

    sb_appendf(cfg, "header = \"Authorization: Bearer %s\"\n", token);
    sb_append(cfg, "header = \"Accept: application/vnd.github+json\"\n");
    sb_append(cfg, "header = \"X-GitHub-Api-Version: 2022-11-28\"\n");
    sb_append(cfg, "header = \"User-Agent: atom-builder\"\n");
    sb_append(cfg, "silent\n");
    sb_append(cfg, "show-error\n");
    return 0;
}

/* Runs curl and splits the trailing status code off the body. `-w` appends it
   on its own line, which is cheaper than a second request to find out. */
static int gh_request(const char *cfg, char *const argv[], StrBuf *body,
                      int *code) {
    StrBuf raw;
    sb_init(&raw);

    int rc = run_with_input(NULL, argv, cfg, strlen(cfg), &raw);
    if (rc != 0) {
        fprintf(stderr, "atom: curl failed (%d)\n", rc);
        sb_free(&raw);
        return -1;
    }

    *code = 0;

    if (raw.data) {
        char *nl = strrchr(raw.data, '\n');
        if (nl) {
            *code = atoi(nl + 1);
            *nl   = '\0';
            sb_append(body, raw.data);
        }
    }

    sb_free(&raw);
    return 0;
}

/* GitHub returns upload_url as a URI template ending in "{?name,label}".
   Trimming the template leaves the endpoint a query string can be added to. */
static void trim_template(char *url) {
    char *brace = strchr(url, '{');
    if (brace) *brace = '\0';
}

/* Only names that need no percent-encoding are accepted, which every archive
   atom produces already satisfies. */
static int name_is_url_safe(const char *s) {
    for (; *s; s++) {
        if ((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') ||
            (*s >= '0' && *s <= '9') || *s == '.' || *s == '-' || *s == '_')
            continue;
        return 0;
    }
    return 1;
}

static int find_release(const char *cfg, const char *repo, const char *tag,
                        char *upload_url, size_t url_size) {
    char url[512];
    snprintf(url, sizeof url, GH_API "/repos/%s/releases/tags/%s", repo, tag);

    ArgV a;
    av_init(&a);
    av_push(&a, "curl");
    av_push(&a, "--config");
    av_push(&a, "-");
    av_push(&a, "-w");
    av_push(&a, "\n%{http_code}");
    av_push(&a, url);

    StrBuf body;
    sb_init(&body);
    int code = 0;
    int rc = gh_request(cfg, a.v, &body, &code);
    av_free(&a);

    if (rc != 0) { sb_free(&body); return -1; }

    if (code == 404) { sb_free(&body); return 1; }     /* no such release yet */

    if (code != 200) {
        fprintf(stderr, "atom: GitHub returned %d looking up tag %s\n%s\n",
                code, tag, body.data ? body.data : "");
        sb_free(&body);
        return -1;
    }

    if (json_string(body.data, "upload_url", upload_url, url_size) != 0) {
        fprintf(stderr, "atom: no upload_url in GitHub's reply\n");
        sb_free(&body);
        return -1;
    }

    sb_free(&body);
    trim_template(upload_url);
    return 0;
}

static int create_release(const char *cfg, const char *repo, const char *tag,
                          const char *name, char *upload_url, size_t url_size) {
    char esc_tag[128], esc_name[128];
    if (json_escape(tag, esc_tag, sizeof esc_tag) != 0 ||
        json_escape(name, esc_name, sizeof esc_name) != 0) {
        fprintf(stderr, "atom: release tag or name is too long\n");
        return -1;
    }

    char payload[512];
    snprintf(payload, sizeof payload,
             "{\"tag_name\":\"%s\",\"name\":\"%s\"}", esc_tag, esc_name);

    char url[512];
    snprintf(url, sizeof url, GH_API "/repos/%s/releases", repo);

    ArgV a;
    av_init(&a);
    av_push(&a, "curl");
    av_push(&a, "--config");
    av_push(&a, "-");
    av_push(&a, "-X");
    av_push(&a, "POST");
    av_push(&a, "-H");
    av_push(&a, "Content-Type: application/json");
    av_push(&a, "-d");
    av_push(&a, payload);
    av_push(&a, "-w");
    av_push(&a, "\n%{http_code}");
    av_push(&a, url);

    StrBuf body;
    sb_init(&body);
    int code = 0;
    int rc = gh_request(cfg, a.v, &body, &code);
    av_free(&a);

    if (rc != 0) { sb_free(&body); return -1; }

    if (code != 201) {
        fprintf(stderr, "atom: GitHub returned %d creating release %s\n%s\n",
                code, tag, body.data ? body.data : "");
        sb_free(&body);
        return -1;
    }

    if (json_string(body.data, "upload_url", upload_url, url_size) != 0) {
        fprintf(stderr, "atom: no upload_url in GitHub's reply\n");
        sb_free(&body);
        return -1;
    }

    sb_free(&body);
    trim_template(upload_url);
    return 0;
}

static int upload_asset(const char *cfg, const char *upload_url,
                        const char *path, const char *name) {
    if (!name_is_url_safe(name)) {
        fprintf(stderr, "atom: %s cannot be used as an asset name\n", name);
        return -1;
    }

    char url[1024];
    snprintf(url, sizeof url, "%s?name=%s", upload_url, name);

    char data[1100];
    snprintf(data, sizeof data, "@%s", path);

    ArgV a;
    av_init(&a);
    av_push(&a, "curl");
    av_push(&a, "--config");
    av_push(&a, "-");
    av_push(&a, "-X");
    av_push(&a, "POST");
    av_push(&a, "-H");
    av_push(&a, "Content-Type: application/octet-stream");
    av_push(&a, "--data-binary");
    av_push(&a, data);
    av_push(&a, "-w");
    av_push(&a, "\n%{http_code}");
    av_push(&a, url);

    StrBuf body;
    sb_init(&body);
    int code = 0;
    int rc = gh_request(cfg, a.v, &body, &code);
    av_free(&a);

    if (rc != 0) { sb_free(&body); return -1; }

    /* 422 is what GitHub says when an asset of that name is already attached;
       it means the release is not in the state the manifest describes, so it
       is reported rather than ignored. */
    if (code == 422) {
        fprintf(stderr, "atom: %s is already attached to this release\n", name);
        sb_free(&body);
        return -1;
    }
    if (code != 201) {
        fprintf(stderr, "atom: GitHub returned %d uploading %s\n%s\n",
                code, name, body.data ? body.data : "");
        sb_free(&body);
        return -1;
    }

    sb_free(&body);
    return 0;
}

int publish_github(const Manifest *m, const BuildOpts *o, const AssetSet *set) {
    char tag[128];
    manifest_tag(m, tag, sizeof tag);

    char destination[512];
    snprintf(destination, sizeof destination, "github.com/%s release %s",
             m->gh_repo, tag);

    if (o->dry_run) {
        printf("\ngithub: would publish %d file%s to %s\n",
               set->count, set->count == 1 ? "" : "s", destination);
        for (int i = 0; i < set->count; i++)
            printf("  %s\n", set->names[i]);
        return 0;
    }

    const char *token = getenv("GITHUB_TOKEN");
    if (!token || !*token) token = getenv("GH_TOKEN");
    if (!token || !*token) {
        fprintf(stderr, "atom: set GITHUB_TOKEN (or GH_TOKEN) to publish to "
                        "GitHub\n");
        return 1;
    }

    if (!o->assume_yes && !publish_confirm(set, destination)) {
        printf("Cancelled.\n");
        return 1;
    }

    StrBuf cfg;
    sb_init(&cfg);
    if (build_curl_config(token, &cfg) != 0) {
        sb_free(&cfg);
        return 1;
    }

    char upload_url[1024];
    int  found = find_release(cfg.data, m->gh_repo, tag,
                              upload_url, sizeof upload_url);

    if (found < 0) { sb_free(&cfg); return 1; }

    if (found == 1) {
        printf("github: creating release %s\n", tag);
        if (create_release(cfg.data, m->gh_repo, tag, tag,
                           upload_url, sizeof upload_url) != 0) {
            sb_free(&cfg);
            return 1;
        }
    } else {
        printf("github: reusing existing release %s\n", tag);
    }

    int failed = 0;
    for (int i = 0; i < set->count; i++) {
        if (upload_asset(cfg.data, upload_url,
                         set->paths[i], set->names[i]) == 0) {
            char size[32];
            human_size(set->sizes[i], size, sizeof size);
            printf("  ok    %-42s %9s\n", set->names[i], size);
        } else {
            printf("  FAIL  %s\n", set->names[i]);
            failed++;
        }
    }

    sb_free(&cfg);

    printf("\ngithub: %d/%d uploaded to %s\n",
           set->count - failed, set->count, destination);

    return failed ? 1 : 0;
}
