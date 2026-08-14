#ifndef INIT_H
#define INIT_H

/* Writes a starting atom.toml into `dir`.

   The manifest is guessed rather than dictated: the files already in the
   directory say which build system the project uses, and the triples are
   spelled the way that system expects them. A template nobody has to correct
   before it runs is worth more than one that merely documents the format.

   An existing atom.toml is never overwritten. Returns 0 on success. */
int cmd_init(const char *dir);

#endif
