---
title: Caching
description: Why the second build takes no time
order: 5
---

Builds are skipped when nothing that could change the result has changed.

```
first build      5.60s
unchanged tree   0.10s
```

```
$ atom build
  ok    linux-x86_64       cached    473 KB  static ELF x86_64
  ok    linux-arm64        cached    462 KB  static ELF aarch64
```

There is nothing to configure. It is on by default and `--no-cache` turns it
off for one run.

## What counts as a change

The cache key covers everything that could produce a different binary:

- the **content** of every file in your source tree
- the exact command that would run, including every flag
- the build system, strategy, container image and setup command
- the environment the build would run in
- `zig version`

> [!TIP]
> Not timestamps. A file's modification time says nothing about whether its
> bytes differ, which is exactly where `make` gets it wrong: `touch` a source
> file and make rebuilds, atom does not.

```bash
touch src/main.c
atom build          # still cached
```

Change a byte, a build flag, or your zig version, and the target rebuilds.

## What is excluded

The walk skips `build/`, `dist/` and `.git`. Those are outputs and metadata,
not inputs. Everything else in the directory counts, including files your build
ignores — atom cannot know which ones matter, and treating an unknown file as
an input is the safe direction to be wrong in.

## A cache hit is still checked

A cached artifact is verified against the target's expected architecture before
it is accepted, exactly as a fresh build would be. A corrupted cache cannot
smuggle the wrong binary into a release.

## Where it lives

`build/.cache/<target>/`, alongside the per-target work trees. Deleting
`build/` clears it. It holds one entry per target, so it does not grow.

## If the cache never hits

The usual cause is a build that writes into your source tree — a log file, a
generated header, a `.o` left behind. Every run then changes the inputs, so
every run is a miss.

Check with:

```bash
atom build
git status          # anything new that the build created?
```

The fix is to have the build write into `build/` or `dist/`, or to a directory
you add to `.gitignore` — though note that atom's walk does not read
`.gitignore`, so a generated file will still count as an input wherever it
lands outside the three excluded directories.

## Correctness details

Three things make the key trustworthy, which are worth knowing if you ever
suspect it:

**Paths are sorted before hashing.** Directory order is not stable between
filesystems or even between runs; without sorting, the same tree could produce
two different keys.

**The path is part of the key.** Moving a file changes the build even when its
bytes are identical.

**The key is written after the artifact is stored.** An interrupted run leaves
a stale artifact with no key — which is simply a miss — rather than a key
promising an artifact that is not there.
