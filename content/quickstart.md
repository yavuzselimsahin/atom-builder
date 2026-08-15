---
title: Quickstart
description: From an existing project to signed-off archives in five commands
order: 2
---

This walks through a real project end to end. If you have a C, C++, Rust, Go,
Zig or .NET project that already builds, you can follow along with it.

## 1. Describe the project

In your project directory:

```bash
atom init
```

```
wrote ./atom.toml

  detected make
  5 targets, project name "atomik-ssg"
  artifact "atomik-ssg", read from the Makefile

Next: check it over, then run `atom targets`.
```

`init` reads the directory rather than printing a blank template. A
`Cargo.toml`, `go.mod`, `build.zig`, `.csproj` or `Makefile` decides the build
system, and the triples it writes are spelled the way that system expects. If
there is a Makefile, it is scanned for the name of the binary it produces.

It never overwrites an `atom.toml` that already exists.

> [!TIP]
> Read what it wrote before running anything. `init` guesses; you know. The
> line worth checking first is `artifact`, because a wrong one fails every
> target at once.

## 2. Check the plan

```bash
atom targets
```

```
atomik-ssg 1.1.0

  ID                 TRIPLE                 EXPECTED         ARTIFACT
  linux-x86_64       x86_64-linux-musl      ELF x86_64       atomik-ssg
  linux-arm64        aarch64-linux-musl     ELF aarch64      atomik-ssg  (verify)
  macos-arm64        aarch64-macos          Mach-O aarch64   atomik-ssg
  macos-x86_64       x86_64-macos           Mach-O x86_64    atomik-ssg
  windows-x86_64     x86_64-windows-gnu     PE x86_64        atomik-ssg.exe
```

Nothing is built. This is the fastest way to check a manifest parses and that
each target resolves to the binary format you expect.

## 3. Build

```bash
atom build
```

```
atomik-ssg 1.1.0 — 5 targets

  ok    linux-x86_64         5.30s    473 KB  static ELF x86_64
  ok    linux-arm64          5.32s    462 KB  static ELF aarch64
  ok    macos-arm64          5.28s    242 KB  Mach-O aarch64
  ok    macos-x86_64         7.02s    204 KB  Mach-O x86_64
  ok    windows-x86_64       5.48s    260 KB  PE x86_64

5/5 succeeded in 7.19s → ./dist/
```

Every target is built in parallel, each in its own copy of your source tree, so
they cannot overwrite each other's object files. The description on the right
(`static ELF aarch64`) is not a guess — atom reads the finished file's own
header and compares it to what the target asked for.

Run it again and it will say `cached`. See [Caching](/guide/caching/).

## 4. Prove the binaries run

```bash
atom verify
```

```
  ok    linux-x86_64         8.85s  ran under docker
  ok    linux-arm64          3.78s  ran under docker
  ok    macos-arm64          0.01s  ran under host
  ok    macos-x86_64         1.86s  ran under host
  skip  windows-x86_64              no wine on this host
```

Compiling for a platform you cannot run is cross-compilation's one real
weakness: you have a binary nobody has ever executed. `verify` closes that,
using emulation where it must. Targets with no available runner are skipped
rather than failed, so a machine without Docker can still build.

## 5. Package

```bash
atom package
```

```
  ok    atomik-ssg-1.1.0-linux-arm64.tar.gz     227 KB  3 files
  ok    atomik-ssg-1.1.0-windows-x86_64.zip     130 KB  3 files
  ok    SHA256SUMS                                      sha256
```

Unix targets get `.tar.gz`, Windows targets get `.zip`, each holding one
`<name>-<version>-<target>/` directory. Verify them the way your users will:

```bash
cd dist && shasum -a 256 -c SHA256SUMS
```

## 6. Publish

Add a destination to `atom.toml`:

```toml
[publish.github]
repo = "you/your-project"
```

Then look before you leap:

```bash
atom publish --dry-run     # shows exactly what would be uploaded
export GITHUB_TOKEN=ghp_...
atom publish               # asks for confirmation first
```

## The short version

Steps 3 to 5 chain into one command:

```bash
atom release
```

That is build, then verify, then package — each step conditional on the last
one fully succeeding, so a partial set of archives never gets written.
Publishing stays separate, because anything that leaves your machine should be
a deliberate act.

## What to read next

- [The manifest](/guide/manifest/) — how `atom.toml` is put together
- [Build systems](/guide/build-systems/) — if `init` guessed wrong, or you use
  something it does not know
- [Commands](/reference/commands/) — every command and flag
