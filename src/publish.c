#include "../include/publish.h"
#include "../include/util.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

int assets_collect(const Manifest *m, const BuildOpts *o, AssetSet *set) {
    set->count = 0;

    for (int i = 0; i < m->target_count && set->count < MAX_ASSETS; i++) {
        const Target *t = &m->targets[i];
        if (o->only && strcmp(o->only, t->id) != 0) continue;

        char base[256];
        target_archive(m, t, base, sizeof base);

        char path[1024];
        snprintf(path, sizeof path, "%s/%s", o->dist_dir, base);

        struct stat st;
        if (stat(path, &st) != 0) {
            fprintf(stderr, "atom: %s is missing — run `atom package` first\n",
                    base);
            return -1;
        }

        int n = set->count++;
        snprintf(set->paths[n], sizeof set->paths[n], "%s", path);
        snprintf(set->names[n], sizeof set->names[n], "%s", base);
        set->sizes[n] = (long long)st.st_size;
    }

    if (set->count == 0) {
        fprintf(stderr, "atom: nothing to publish\n");
        return -1;
    }

    /* SHA256SUMS covers the whole release, so it only rides along when the
       whole release is being published. */
    if (m->checksum && !o->only && set->count < MAX_ASSETS) {
        char path[1024];
        snprintf(path, sizeof path, "%s/SHA256SUMS", o->dist_dir);

        struct stat st;
        if (stat(path, &st) == 0) {
            int n = set->count++;
            snprintf(set->paths[n], sizeof set->paths[n], "%s", path);
            snprintf(set->names[n], sizeof set->names[n], "SHA256SUMS");
            set->sizes[n] = (long long)st.st_size;
        }
    }

    return 0;
}

/* Publishing is outward-facing and hard to take back, so it is confirmed
   unless the caller has already said yes. */
int publish_confirm(const AssetSet *set, const char *destination) {
    printf("\nAbout to publish %d file%s to %s:\n\n",
           set->count, set->count == 1 ? "" : "s", destination);

    for (int i = 0; i < set->count; i++) {
        char size[32];
        human_size(set->sizes[i], size, sizeof size);
        printf("  %-42s %9s\n", set->names[i], size);
    }

    printf("\nContinue? [y/N]: ");
    fflush(stdout);

    char answer[16];
    if (!fgets(answer, sizeof answer, stdin)) return 0;
    return answer[0] == 'y' || answer[0] == 'Y';
}

int cmd_publish(const Manifest *m, const BuildOpts *o) {
    if (!m->ssh_path[0] && !m->gh_repo[0]) {
        fprintf(stderr,
                "atom: nothing configured to publish to.\n\n"
                "Add a destination to the manifest:\n\n"
                "  [publish.ssh]\n"
                "  host = \"user@vps\"\n"
                "  path = \"/var/www/dl/%s\"\n\n"
                "  [publish.github]\n"
                "  repo = \"owner/%s\"\n", m->name, m->name);
        return 1;
    }

    AssetSet set;
    if (assets_collect(m, o, &set) != 0) return 1;

    int failed = 0;
    if (m->ssh_path[0] && publish_ssh(m, o, &set) != 0)    failed++;
    if (m->gh_repo[0]  && publish_github(m, o, &set) != 0) failed++;

    return failed;
}
