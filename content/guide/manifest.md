---
title: The manifest
description: How atom.toml is put together
order: 1
---

Everything atom knows about your project lives in one file next to your build
script. `atom init` writes a working one; this explains what it wrote.

## A complete example

```toml
[project]
name    = "atomik-ssg"
version = "1.1.0"

[build]
system   = "make"
artifact = "atomik-ssg"
strip    = true

[verify]
expect = "static site generator"

[package]
include  = "README.md LICENSE"
checksum = true

[publish.github]
repo = "yavuzselimsahin/atomik-ssg"

[target.linux-x86_64]
triple = "x86_64-linux-musl"

[target.linux-arm64]
triple = "aarch64-linux-musl"
verify = true

[target.macos-arm64]
triple = "aarch64-macos"

[target.windows-x86_64]
triple   = "x86_64-windows-gnu"
artifact = "atomik-ssg.exe"
make     = "BIN=atomik-ssg.exe LIBS=-lws2_32"
```

Only three things are strictly required: a project `name`, a `build.artifact`,
and at least one target.

## Targets are named, not numbered

Each target is its own section, and the part after `target.` is its id:

```toml
[target.linux-arm64]
triple = "aarch64-linux-musl"
```

That id is what you pass to `-t`, what names the directory under `dist/`, and
what appears in the archive filename. Pick something you would not mind seeing
on a download page.

Targets are built in parallel, so their order in the file means nothing.

## The triple is the source of truth

A target's `triple` names the platform, and atom derives everything else from
it: the binary format it should produce, the architecture to check the result
against, which emulator can run it, and — for toolchains that spell platforms
differently — their own identifiers.

```
aarch64-linux-musl
   │      │     └── libc: musl links statically, gnu links dynamically
   │      └──────── operating system
   └─────────────── architecture
```

Use the spelling your build system expects. zig, go, .NET and custom scripts
take zig-style triples (`x86_64-linux-musl`); cargo takes Rust's
(`x86_64-unknown-linux-musl`). `atom init` writes the right ones for whichever
system it detected.

> [!TIP]
> Prefer `-musl` for anything you distribute. musl targets link statically, so
> the result depends on nothing at runtime. `-gnu` targets link dynamically
> against whatever glibc happens to be on the user's machine.

## Two settings named `artifact`

This trips people up once.

`[build] artifact` is the *name of the file your build produces*. Not a path,
not the project name — the filename.

`[target.…] artifact` overrides that for one target. Windows is the usual
reason:

```toml
[target.windows-x86_64]
triple   = "x86_64-windows-gnu"
artifact = "myapp.exe"
```

Where that file *lands* is a separate question, answered by the build system —
make leaves it in the tree root, cargo puts it under `target/<triple>/release/`.
See [Build systems](/guide/build-systems/).

## Passing arguments to your build

`[target.…] make` appends arguments to the build command for that target only.
Despite the name it works for any build system; it is a general escape hatch.

```toml
[target.windows-x86_64]
make = "BIN=myapp.exe LIBS=-lws2_32"
```

It is appended last, so it can override anything atom set. A Makefile that
picks its output name behind an `ifeq ($(OS),Windows_NT)` check needs this:
that check never fires while cross-compiling, so the variable has to be passed
in.

## Sections you can leave out

<table>
<thead><tr><th>Section</th><th>Leave it out and…</th></tr></thead>
<tbody>
<tr><td><code>[verify]</code></td><td>binaries are run with no arguments and only their exit status is checked</td></tr>
<tr><td><code>[package]</code></td><td>archives hold just the binary, and <code>SHA256SUMS</code> is still written</td></tr>
<tr><td><code>[publish.*]</code></td><td><code>atom publish</code> tells you there is nowhere to publish to</td></tr>
</tbody>
</table>

## Parser notes

atom reads a deliberately small subset of TOML. Two behaviours are worth
knowing:

**Arrays of tables are not supported.** Write `[target.linux-arm64]`, not
`[[target]]`. This is why every target has a name.

> [!CAUTION]
> An empty value means *not set*, not *disabled*. Writing `clean = ""` falls
> back to the default. To disable a command, give it one that does nothing:

```toml
[build]
clean = "true"
```

Inline comments are stripped, so this does what it looks like:

```toml
artifact = "myapp.exe"    # windows only
```
