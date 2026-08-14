#include "../include/publish.h"
#include "../include/exec.h"
#include "../include/util.h"

#include <stdio.h>
#include <string.h>

/* Mirroring to a server is `rsync`, exactly as it would be typed by hand. The
   destination is built as one argv element, so a path with spaces needs no
   quoting and no shell is involved. */
int publish_ssh(const Manifest *m, const BuildOpts *o, const AssetSet *set) {
    char dest[1024];

    if (m->ssh_host[0])
        snprintf(dest, sizeof dest, "%s:%s/", m->ssh_host, m->ssh_path);
    else
        snprintf(dest, sizeof dest, "%s/", m->ssh_path);

    if (o->dry_run) {
        printf("\nssh: would upload %d file%s to %s\n",
               set->count, set->count == 1 ? "" : "s", dest);
        for (int i = 0; i < set->count; i++)
            printf("  %s\n", set->names[i]);
        return 0;
    }

    if (!o->assume_yes && !publish_confirm(set, dest)) {
        printf("Cancelled.\n");
        return 1;
    }

    /* rsync will not create intermediate directories, so the destination is
       made first — remotely over ssh, locally with mkdir. */
    ArgV mk;
    av_init(&mk);
    if (m->ssh_host[0]) {
        av_push(&mk, "ssh");
        av_push(&mk, m->ssh_host);
        av_push(&mk, "mkdir");
        av_push(&mk, "-p");
        av_push(&mk, m->ssh_path);
    } else {
        av_push(&mk, "mkdir");
        av_push(&mk, "-p");
        av_push(&mk, m->ssh_path);
    }

    int rc = run_sync(NULL, mk.v, NULL);
    av_free(&mk);

    if (rc != 0) {
        fprintf(stderr, "atom: could not create %s\n", m->ssh_path);
        return 1;
    }

    ArgV a;
    av_init(&a);
    av_push(&a, "rsync");
    av_push(&a, "-av");
    for (int i = 0; i < set->count; i++) av_push(&a, set->paths[i]);
    av_push(&a, dest);

    StrBuf out;
    sb_init(&out);
    rc = run_sync(NULL, a.v, &out);
    av_free(&a);

    if (rc != 0) {
        fprintf(stderr, "atom: rsync failed (%d)\n%s\n", rc,
                out.data ? out.data : "");
        sb_free(&out);
        return 1;
    }
    sb_free(&out);

    printf("\nssh: %d file%s → %s\n", set->count,
           set->count == 1 ? "" : "s", dest);
    return 0;
}
