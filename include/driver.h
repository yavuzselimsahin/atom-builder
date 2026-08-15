#ifndef DRIVER_H
#define DRIVER_H

#include "manifest.h"
#include "util.h"

#define CONTAINER_ID_LEN 80

/* The container strategy builds inside the target's own userland rather than
   cross-compiling into it. That is what makes it worth the cost: a configure
   script can run the binaries it just built, and the result links against that
   distribution's actual libraries. Docker supplies the emulation, so a
   linux/amd64 build works on Apple silicon without any qemu setup here.

   The image must already carry a toolchain, or `setup` must install one. */

/* Starts a detached container with `workdir` mounted at /src. Writes its id to
   `id_out`. Returns 0 on success. */
int container_start(const Target *t, const char *workdir,
                    char *id_out, size_t id_size);

/* Runs the target's `setup` inside the container. This is the one place a
   shell is used, because `setup` is written as a shell command and runs in the
   container rather than on the host. Returns 0 on success, and writes any
   output to `log`. */
int container_setup(const Target *t, const char *id, StrBuf *log);

/* Builds the argv that runs `command` inside the container. No shell: the
   build command is passed through as arguments. */
int container_exec_argv(const char *id, char *const command[], ArgV *out);

/* Removes the container. Safe to call with an empty id. */
void container_stop(const char *id);

/* Docker pulls an image the first time it is used, and leaves it on the
   machine afterwards. Somebody who ran one build should not discover a
   two-gigabyte image they never asked for, so atom records which images it
   introduced and can take exactly those back out again.

   An image that was already present is never touched: it was not atom's to
   remove, and somebody is probably using it. */

/* Notes that `image` is about to be used, remembering whether this machine
   already had it. Safe to call repeatedly with the same name. */
void image_note(const char *image);

/* Removes the images atom introduced during this run. With `keep` non-zero it
   only reports them, which is what you want while iterating. */
void image_cleanup(int keep);

#endif
