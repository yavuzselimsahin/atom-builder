#include "../include/manifest.h"
#include "../include/buildsys.h"
#include "../toml.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#define TARGET_PREFIX "target."

static void copy_field(char *dst, size_t dst_size, const char *src) {
    snprintf(dst, dst_size, "%s", src ? src : "");
}

int manifest_load(const char *path, Manifest *m) {
    TomlDoc doc;

    if (toml_parse(path, &doc) != 0) {
        fprintf(stderr, "atom: cannot read manifest: %s\n", path);
        return -1;
    }
    if (doc.truncated) {
        fprintf(stderr, "atom: %s has more than %d keys; the rest were "
                        "ignored\n", path, TOML_MAX_PAIRS);
        return -1;
    }

    memset(m, 0, sizeof *m);

    copy_field(m->name,     sizeof m->name,
               toml_get_or(&doc, "project", "name", ""));
    copy_field(m->version,  sizeof m->version,
               toml_get_or(&doc, "project", "version", "0.0.0"));

    /* The build system supplies the defaults for command, output and clean;
       anything set here overrides it. Leaving them empty is the normal case. */
    copy_field(m->system,   sizeof m->system,
               toml_get_or(&doc, "build", "system", "make"));
    copy_field(m->command,  sizeof m->command,
               toml_get_or(&doc, "build", "command", ""));
    copy_field(m->artifact, sizeof m->artifact,
               toml_get_or(&doc, "build", "artifact", ""));
    copy_field(m->output,   sizeof m->output,
               toml_get_or(&doc, "build", "output", ""));
    copy_field(m->clean,    sizeof m->clean,
               toml_get_or(&doc, "build", "clean", ""));
    m->strip = toml_get_bool(&doc, "build", "strip", 1);

    const BuildSystem *sys = buildsys_find(m->system);
    if (!sys) {
        fprintf(stderr, "atom: %s: [build] system '%s' is not one of: %s\n",
                path, m->system, buildsys_names());
        return -1;
    }
    if (strcmp(m->system, "custom") == 0 && (!m->command[0] || !m->output[0])) {
        fprintf(stderr, "atom: %s: [build] system is custom, so it needs both "
                        "command and output\n", path);
        return -1;
    }

    copy_field(m->format,  sizeof m->format,
               toml_get_or(&doc, "package", "format", "tar.gz"));
    copy_field(m->include, sizeof m->include,
               toml_get_or(&doc, "package", "include", ""));
    m->checksum = toml_get_bool(&doc, "package", "checksum", 1);

    if (strcmp(m->format, "tar.gz") != 0 && strcmp(m->format, "zip") != 0) {
        fprintf(stderr, "atom: %s: [package] format must be tar.gz or zip, "
                        "not '%s'\n", path, m->format);
        return -1;
    }

    copy_field(m->verify_args,   sizeof m->verify_args,
               toml_get_or(&doc, "verify", "args", ""));
    copy_field(m->verify_expect, sizeof m->verify_expect,
               toml_get_or(&doc, "verify", "expect", ""));
    copy_field(m->verify_image,  sizeof m->verify_image,
               toml_get_or(&doc, "verify", "image", "alpine:3.20"));

    {
        const char *v = toml_get_or(&doc, "verify", "exit", "0");
        m->verify_exit = atoi(v);
        v = toml_get_or(&doc, "verify", "timeout", "60");
        m->verify_timeout = atoi(v);
        if (m->verify_timeout <= 0) m->verify_timeout = 60;
    }

    copy_field(m->ssh_host, sizeof m->ssh_host,
               toml_get_or(&doc, "publish.ssh", "host", ""));
    copy_field(m->ssh_path, sizeof m->ssh_path,
               toml_get_or(&doc, "publish.ssh", "path", ""));
    copy_field(m->gh_repo,  sizeof m->gh_repo,
               toml_get_or(&doc, "publish.github", "repo", ""));
    copy_field(m->gh_tag,   sizeof m->gh_tag,
               toml_get_or(&doc, "publish.github", "tag", ""));

    if (m->ssh_host[0] && !m->ssh_path[0]) {
        fprintf(stderr, "atom: %s: [publish.ssh] host is set but path is "
                        "missing\n", path);
        return -1;
    }
    if (m->gh_repo[0] && !strchr(m->gh_repo, '/')) {
        fprintf(stderr, "atom: %s: [publish.github] repo must be owner/name, "
                        "not '%s'\n", path, m->gh_repo);
        return -1;
    }

    if (!m->name[0]) {
        fprintf(stderr, "atom: %s: [project] name is required\n", path);
        return -1;
    }
    if (!m->artifact[0]) {
        fprintf(stderr, "atom: %s: [build] artifact is required — name the "
                        "file the build produces\n", path);
        return -1;
    }

    int iter = 0;
    const char *section;
    while ((section = toml_section_next(&doc, TARGET_PREFIX, &iter))) {
        const char *id = section + strlen(TARGET_PREFIX);

        if (!*id) {
            fprintf(stderr, "atom: %s: a [target.<id>] section is missing "
                            "its id\n", path);
            return -1;
        }
        if (m->target_count >= MAX_TARGETS) {
            fprintf(stderr, "atom: %s: more than %d targets\n",
                    path, MAX_TARGETS);
            return -1;
        }

        Target *t = &m->targets[m->target_count];

        copy_field(t->id,        sizeof t->id,        id);
        copy_field(t->triple,    sizeof t->triple,
                   toml_get_or(&doc, section, "triple", ""));
        copy_field(t->artifact,  sizeof t->artifact,
                   toml_get_or(&doc, section, "artifact", ""));
        copy_field(t->make_args, sizeof t->make_args,
                   toml_get_or(&doc, section, "make", ""));
        copy_field(t->format,    sizeof t->format,
                   toml_get_or(&doc, section, "format", ""));
        copy_field(t->strategy,  sizeof t->strategy,
                   toml_get_or(&doc, section, "strategy", "zig"));
        copy_field(t->image,     sizeof t->image,
                   toml_get_or(&doc, section, "image", ""));
        copy_field(t->setup,     sizeof t->setup,
                   toml_get_or(&doc, section, "setup", ""));
        t->verify = toml_get_bool(&doc, section, "verify", 0);

        if (strcmp(t->strategy, "zig")       != 0 &&
            strcmp(t->strategy, "container") != 0 &&
            strcmp(t->strategy, "native")    != 0) {
            fprintf(stderr, "atom: %s: [%s] strategy must be zig, container "
                            "or native, not '%s'\n", path, section,
                            t->strategy);
            return -1;
        }
        if (strcmp(t->strategy, "container") == 0 && !t->image[0]) {
            fprintf(stderr, "atom: %s: [%s] strategy is container, so it "
                            "needs an image\n", path, section);
            return -1;
        }

        if (t->format[0] && strcmp(t->format, "tar.gz") != 0 &&
            strcmp(t->format, "zip") != 0) {
            fprintf(stderr, "atom: %s: [%s] format must be tar.gz or zip, "
                            "not '%s'\n", path, section, t->format);
            return -1;
        }
        if (!t->triple[0]) {
            fprintf(stderr, "atom: %s: [%s] needs a triple\n", path, section);
            return -1;
        }
        if (manifest_find(m, t->id)) {
            fprintf(stderr, "atom: %s: target '%s' is declared twice\n",
                    path, t->id);
            return -1;
        }

        m->target_count++;
    }

    if (m->target_count == 0) {
        fprintf(stderr, "atom: %s: no targets declared — add at least one "
                        "[target.<id>] section\n", path);
        return -1;
    }

    return 0;
}

const char *target_artifact(const Manifest *m, const Target *t) {
    return t->artifact[0] ? t->artifact : m->artifact;
}

const Target *manifest_find(const Manifest *m, const char *id) {
    for (int i = 0; i < m->target_count; i++) {
        if (strcmp(m->targets[i].id, id) == 0) return &m->targets[i];
    }
    return NULL;
}

const char *target_format(const Manifest *m, const Target *t) {
    if (t->format[0]) return t->format;
    return strstr(t->triple, "-windows") ? "zip" : m->format;
}

void target_prefix(const Manifest *m, const Target *t, char *out, size_t size) {
    snprintf(out, size, "%s-%s-%s", m->name, m->version, t->id);
}

void target_archive(const Manifest *m, const Target *t, char *out, size_t size) {
    char prefix[256];
    target_prefix(m, t, prefix, sizeof prefix);
    snprintf(out, size, "%s.%s", prefix, target_format(m, t));
}

const char *manifest_template(const Manifest *m, const char *field) {
    const BuildSystem *sys = buildsys_find(m->system);

    if (strcmp(field, "command") == 0)
        return m->command[0] ? m->command : (sys ? sys->command : "");
    if (strcmp(field, "output") == 0)
        return m->output[0]  ? m->output  : (sys ? sys->output  : "");
    if (strcmp(field, "clean") == 0)
        return m->clean[0]   ? m->clean   : (sys ? sys->clean   : "");

    return "";
}

void manifest_tag(const Manifest *m, char *out, size_t size) {
    if (m->gh_tag[0]) snprintf(out, size, "%s", m->gh_tag);
    else              snprintf(out, size, "v%s", m->version);
}
