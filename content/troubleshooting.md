---
title: Troubleshooting
description: What the errors mean and how to fix them
order: 6
---

atom tries to say what went wrong and what to do about it. This is the longer
version of each message.

## Build

### wrong artifact: expected ELF aarch64, got Mach-O aarch64

The build exited 0 but produced a file for the wrong platform. Almost always
this means `make` decided there was nothing to do and left an earlier binary in
place, because stale object files from another architecture were in the tree.

Check `[build] clean`. It runs before every build precisely to prevent this,
and disabling it — or pointing it at a rule that does not remove object files —
brings the problem back.

This is the check that exists because a build exiting 0 is not proof of
anything.

### build reported success but ./build/…/myapp was never created

`[build] artifact` does not name the file your build actually produces.

For Windows targets, remember the name usually changes: `myapp` becomes
`myapp.exe`, and with `make` you also have to tell the Makefile, since its
`ifeq ($(OS),Windows_NT)` check never fires while cross-compiling.

```toml
[target.windows-x86_64]
artifact = "myapp.exe"
make     = "BIN=myapp.exe"
```

If you use a non-`make` system, check `[build] output` too — cargo puts its
binary under `target/<triple>/release/`, not in the tree root.

### myapp is not a recognisable executable

The file at that path exists but is not an ELF, Mach-O or PE binary. Usually
`artifact` is pointing at a library, an object file, or a script.

### "…" uses {tripel}, which is not available for this target

A placeholder was misspelled. The full list is in the
[reference](/reference/manifest-keys/#placeholders). atom fails here rather than
passing the literal text to your compiler.

### Cache never hits

Something in your build writes into the source tree, so every run changes the
inputs. Run a build and then `git status` to see what appeared. See
[Caching](/guide/caching/#if-the-cache-never-hits).

## Verify

### exited 2, expected 0

The binary ran but returned something other than 0. Many programs do this when
invoked with no arguments — printing usage and exiting non-zero is common.

Give it something that succeeds:

```toml
[verify]
args   = "--version"
expect = "1.2"
```

### output did not contain "…"

The binary ran and exited as expected, but `expect` was not in its output.
Either the string is wrong, or the program prints it somewhere `expect` does
not see. atom captures both stdout and stderr, so it is usually the former.

### needs qemu-user or a running docker

There is no way to execute this target on this machine. Start Docker, or
install `qemu-user` if you are on Linux, or accept the skip.

If the target is marked `verify = true`, the skip is a failure by design —
remove the flag if you do not want that.

### exited 133, or a list of missing shared libraries

The binary and the verification image disagree about libc. A `-gnu` triple
produces a glibc binary, which cannot run in the musl-based default image.

```toml
[verify]
image = "debian:12-slim"
```

Run `atom verify -v` to see the actual loader errors.

### did not finish within 60s

The binary hung — often waiting for input that will never come, or running very
slowly under emulation. Raise `[verify] timeout`, or point `args` at something
that returns immediately.

## Container builds

### setup failed inside alpine:3.20

The command in `setup` failed inside the container. Run with `-v` to see its
output; it is usually a wrong package name or no network access.

### could not start &lt;image&gt;

Docker is not running, or the image cannot be pulled, or Docker has no platform
for that architecture.

## Package

### [package] include names assets, which is a directory

`include` takes files, not directories. atom ships one binary per target, and
the archive writers do not walk a tree, so a directory would otherwise land in
the archive as a zero-byte file that looks fine until someone extracts it.

List the files, or have your build produce a single archive and point
`artifact` at that.

## Publish

### atomik-ssg-1.1.0-linux-arm64.tar.gz is missing — run `atom package` first

You are publishing archives that have not been made yet. If you just changed
`version`, the old archives are still there under the old name — clear `dist/`
and run `atom release`.

### set GITHUB_TOKEN (or GH_TOKEN) to publish to GitHub

No token in the environment.

```bash
export GITHUB_TOKEN=ghp_...
```

### GitHub returned 404 looking up tag v1.2.0

Not actually about the tag — a 404 from the GitHub API usually means the token
cannot see the repository. Check the repo name and that the token has
**Contents: read and write** on it.

### … is already attached to this release

An asset of that name is on the release already. atom stops rather than
guessing whether you meant to replace it. Delete the old asset, or use a
different `tag`.

### The release points at the wrong code

If the tag did not already exist, GitHub created it at the head of your default
branch — not at your local `HEAD`. Push first, then publish.

## Manifest

### [build] artifact is required

Every manifest needs to know what file the build produces.

### no targets declared

Add at least one `[target.<id>]` section.

### [build] system 'bazel' is not one of: make, cargo, go, zig, dotnet, custom

Use `custom` with your own `command` and `output`. See
[Build systems](/guide/build-systems/#custom).

### [target.x] strategy is container, so it needs an image

A container build has nowhere to run without one.

### atom.toml already exists — nothing was changed

`atom init` will not overwrite a manifest. Move or delete the existing one if
you really want a fresh start.

## Still stuck

Run with `-v`. Every command shows the underlying tool's own output on failure,
and `verify -v` explains each skip. The problem is usually in that output
rather than in atom's summary of it.
