---
title: Install
description: Get atom onto your machine and onto your PATH
order: 1
---

atom is a single C program with no dependencies beyond libc. Building it takes
a few seconds.

```bash
git clone https://github.com/yavuzselimsahin/atom-builder.git
cd atom-builder
make
make install PREFIX=~/.local
```

`~/.local/bin` is already on `PATH` on most systems and needs no `sudo`. Leave
`PREFIX` out to install into `/usr/local/bin` instead, which does.

Check it worked:

```bash
atom version
```

## What else you need

atom drives other programs rather than reimplementing them. Which ones you
need depends on what you ask it to do.

<table>
<thead>
<tr><th>Tool</th><th>Needed for</th><th>When</th></tr>
</thead>
<tbody>
<tr><td><code>cc</code>, <code>make</code></td><td>Building atom itself</td><td>Always</td></tr>
<tr><td><a href="https://ziglang.org">zig</a></td><td>Cross-compilers</td><td>Any target using the default <code>zig</code> strategy</td></tr>
<tr><td>Your own toolchain</td><td>Compiling your project</td><td>cargo, go, dotnet — whatever your project uses</td></tr>
<tr><td><code>gzip</code></td><td>Compression</td><td><code>atom package</code></td></tr>
<tr><td><code>rsync</code>, <code>ssh</code></td><td>Uploading to a server</td><td><code>atom publish</code> with an ssh destination</td></tr>
<tr><td><code>curl</code></td><td>GitHub Releases API</td><td><code>atom publish</code> with a GitHub destination</td></tr>
<tr><td><code>docker</code></td><td>Emulation</td><td>Container builds, and verifying Linux binaries from a non-Linux host</td></tr>
<tr><td><code>wine</code></td><td>Running Windows binaries</td><td>Verifying Windows targets</td></tr>
</tbody>
</table>

Only the first two are needed to build anything at all. The rest are checked
when a command actually needs them, and atom says which one is missing rather
than failing obscurely.

## Installing zig

zig supplies the cross-compilers that make one machine able to build for all
the others. Any recent version works.

```bash
brew install zig          # macOS
```

Or download a build from [ziglang.org/download](https://ziglang.org/download/)
and put it on your `PATH`. atom only ever calls `zig cc` and `zig version`.

## Where atom runs

atom uses `fork`, `execvp` and `poll`, so it runs on **macOS, Linux and the
BSDs**.

> [!NOTE]
> Windows is a first-class *target* — atom builds Windows binaries happily
> from any of those hosts — but atom does not yet *run* on Windows.

## Working on atom itself

If you are changing atom's own source, `make link` symlinks the binary instead
of copying it, so a rebuild takes effect immediately:

```bash
make link PREFIX=~/.local
```

> [!CAUTION]
> `make clean` then leaves a dangling link until you rebuild, which shows up
> as `command not found`. Use `make install` for a copy that survives.

`make uninstall PREFIX=~/.local` removes either kind.
