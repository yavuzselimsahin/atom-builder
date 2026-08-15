---
title: Install
description: Download a build, or compile it yourself
order: 1
---

Two ways in. Downloading a build is faster; building from source needs nothing
but a C compiler and takes a few seconds.

## Download a build

Take the archive for your platform from the
[latest release](https://github.com/yavuzselimsahin/atom-builder/releases/latest),
or use one of these.

### macOS, Apple silicon

```bash
curl -LO https://github.com/yavuzselimsahin/atom-builder/releases/latest/download/atom-builder-1.1.0-macos-arm64.zip
unzip atom-builder-1.1.0-macos-arm64.zip
xattr -d com.apple.quarantine atom-builder-1.1.0-macos-arm64/atom
mkdir -p ~/.local/bin && mv atom-builder-1.1.0-macos-arm64/atom ~/.local/bin/
```

### macOS, Intel

```bash
curl -LO https://github.com/yavuzselimsahin/atom-builder/releases/latest/download/atom-builder-1.1.0-macos-x86_64.zip
unzip atom-builder-1.1.0-macos-x86_64.zip
xattr -d com.apple.quarantine atom-builder-1.1.0-macos-x86_64/atom
mkdir -p ~/.local/bin && mv atom-builder-1.1.0-macos-x86_64/atom ~/.local/bin/
```

### Linux, x86_64

```bash
curl -LO https://github.com/yavuzselimsahin/atom-builder/releases/latest/download/atom-builder-1.1.0-linux-x86_64.tar.gz
tar xzf atom-builder-1.1.0-linux-x86_64.tar.gz
mkdir -p ~/.local/bin && mv atom-builder-1.1.0-linux-x86_64/atom ~/.local/bin/
```

### Linux, arm64

```bash
curl -LO https://github.com/yavuzselimsahin/atom-builder/releases/latest/download/atom-builder-1.1.0-linux-arm64.tar.gz
tar xzf atom-builder-1.1.0-linux-arm64.tar.gz
mkdir -p ~/.local/bin && mv atom-builder-1.1.0-linux-arm64/atom ~/.local/bin/
```

`~/.local/bin` is on `PATH` on most systems and needs no `sudo`. Check it
worked:

```bash
atom version
```

> [!NOTE]
> The macOS builds are unsigned, so Gatekeeper quarantines anything arriving
> through a browser or `curl` and refuses to run it. The `xattr -d
> com.apple.quarantine` line clears that flag. Signing and notarization are
> planned; until then the step is unavoidable.

### Check what you downloaded

Every release carries a `SHA256SUMS` covering its archives.

```bash
curl -LO https://github.com/yavuzselimsahin/atom-builder/releases/latest/download/SHA256SUMS
shasum -a 256 -c SHA256SUMS --ignore-missing     # macOS
sha256sum -c SHA256SUMS --ignore-missing         # Linux
```

`--ignore-missing` checks the archive you actually downloaded instead of
complaining about the three you did not.

## Build from source

The whole program is C with no dependencies beyond libc, so this takes seconds.

```bash
git clone https://github.com/yavuzselimsahin/atom-builder.git
cd atom-builder
make
make install PREFIX=~/.local
```

Leave `PREFIX` out to install into `/usr/local/bin` instead, which needs
`sudo`. `make uninstall PREFIX=~/.local` removes it again.

Building from source also gets you the test suite:

```bash
make test
```

> [!TIP]
> If you are going to work on atom itself, `make link` symlinks the binary
> rather than copying it, so a rebuild takes effect with no second step. The
> catch is that `make clean` then leaves a dangling link until you build again.

## What else you need

atom drives other programs rather than reimplementing them. Which ones you need
depends on what you ask it to do.

<table>
<thead>
<tr><th>Tool</th><th>Needed for</th><th>When</th></tr>
</thead>
<tbody>
<tr><td><a href="https://ziglang.org">zig</a></td><td>Cross-compilers</td><td>Any target using the default <code>zig</code> strategy</td></tr>
<tr><td>Your own toolchain</td><td>Compiling your project</td><td>cargo, go, dotnet — whatever your project uses</td></tr>
<tr><td><code>gzip</code></td><td>Compression</td><td><code>atom package</code></td></tr>
<tr><td><code>rsync</code>, <code>ssh</code></td><td>Uploading to a server</td><td><code>atom publish</code> with an ssh destination</td></tr>
<tr><td><code>curl</code></td><td>GitHub Releases API</td><td><code>atom publish</code> with a GitHub destination</td></tr>
<tr><td><code>docker</code></td><td>Emulation</td><td>Container builds, and verifying Linux binaries from a non-Linux host</td></tr>
<tr><td><code>wine</code></td><td>Running Windows binaries</td><td>Verifying Windows targets</td></tr>
<tr><td><code>cc</code>, <code>make</code></td><td>Building atom itself</td><td>Only if you build from source</td></tr>
</tbody>
</table>

Each is checked when a command actually needs it, and atom names the missing
one rather than failing obscurely.

### Installing zig

zig supplies the cross-compilers that let one machine build for all the others.
Any recent version works.

```bash
brew install zig          # macOS
```

Or take a build from [ziglang.org/download](https://ziglang.org/download/) and
put it on your `PATH`. atom only ever calls `zig cc` and `zig version`.

## Where atom runs

atom uses `fork`, `execvp` and `poll`, so it runs on **macOS, Linux and the
BSDs**.

> [!IMPORTANT]
> Windows is a first-class *target* — atom produces Windows binaries from any
> of those hosts — but atom itself does not yet *run* on Windows. That would
> need a second implementation of its process handling on top of
> `CreateProcess`, which is why the release carries no Windows build.
