---
title: Coming from CI
description: How atom compares to a GitHub Actions release workflow
order: 3
---

Most projects that ship binaries do it with a CI matrix. This is what that
looks like beside atom, and where each one is the better answer.

## The same release, twice

A typical Actions workflow for a C project that ships five platforms:

```yaml
name: Release
on:
  push:
    tags: ["v*"]

jobs:
  build:
    strategy:
      matrix:
        include:
          - os: ubuntu-latest
            target: linux-x86_64
          - os: ubuntu-24.04-arm
            target: linux-arm64
          - os: macos-latest
            target: macos-arm64
          - os: macos-13
            target: macos-x86_64
          - os: windows-latest
            target: windows-x86_64
    runs-on: ${{ matrix.os }}
    steps:
      - uses: actions/checkout@v4
      - run: make
      - run: tar czf ${{ matrix.target }}.tar.gz myapp
      - uses: actions/upload-artifact@v4
        with:
          name: ${{ matrix.target }}
          path: ${{ matrix.target }}.tar.gz

  release:
    needs: build
    runs-on: ubuntu-latest
    steps:
      - uses: actions/download-artifact@v4
      - run: |
          sha256sum */*.tar.gz > SHA256SUMS
          gh release create ${{ github.ref_name }} */*.tar.gz SHA256SUMS
        env:
          GITHUB_TOKEN: ${{ secrets.GITHUB_TOKEN }}
```

The same thing in atom:

```toml
[project]
name    = "myapp"
version = "1.0.0"

[build]
artifact = "myapp"

[publish.github]
repo = "you/myapp"

[target.linux-x86_64]
triple = "x86_64-linux-musl"

[target.linux-arm64]
triple = "aarch64-linux-musl"

[target.macos-arm64]
triple = "aarch64-macos"

[target.macos-x86_64]
triple = "x86_64-macos"

[target.windows-x86_64]
triple   = "x86_64-windows-gnu"
artifact = "myapp.exe"
make     = "BIN=myapp.exe"
```

```bash
atom release && atom publish
```

## Why the difference is so large

They are not doing the same thing.

**Actions rents five machines.** `windows-latest` is a full Windows Server VM
provisioned from scratch for the job; `macos-latest` is real Apple hardware,
because macOS cannot legally be virtualised anywhere else. Each one compiles
natively for itself. Nothing is cross-compiled — the matrix *is* the mechanism.

**atom uses one machine and cross-compiles.** The five targets are five
invocations of a compiler that can already emit code for all of them.

That is the whole trade. One is renting hardware; the other is asking the
toolchain to do what it was already able to do.

## Where each one wins

<table>
<thead><tr><th></th><th>GitHub Actions</th><th>atom</th></tr></thead>
<tbody>
<tr><td>Time for five targets</td><td>minutes, mostly queueing and provisioning</td><td>seconds</td></tr>
<tr><td>Feedback loop</td><td>push, wait, read a log</td><td>local, immediate</td></tr>
<tr><td>Runs on a fork or offline</td><td>no</td><td>yes</td></tr>
<tr><td>Config</td><td>~40 lines of YAML</td><td>~20 lines of TOML</td></tr>
<tr><td>Cost</td><td>free for public repos, metered for private</td><td>your laptop</td></tr>
<tr><td>Runs your test suite on each OS</td><td><strong>yes</strong></td><td>no — it checks that binaries start</td></tr>
<tr><td>Builds things that cannot cross-compile</td><td><strong>yes</strong></td><td>no</td></tr>
<tr><td>Gates pull requests</td><td><strong>yes</strong></td><td>no</td></tr>
<tr><td>Builds on a schedule, or on someone else's push</td><td><strong>yes</strong></td><td>no</td></tr>
</tbody>
</table>

## When you still want CI

atom is not trying to replace continuous integration. It replaces the *release
build* part of it — which for many projects is the only part that needed five
machines.

Keep CI when:

- **Your tests need to run on each OS.** atom verifies that a binary starts,
  not that it is correct. A Windows-specific path bug is exactly the kind of
  thing only a Windows machine will find.
- **Your build cannot cross-compile.** PyInstaller and friends bundle the host
  interpreter and its native extensions, so a Windows executable needs a
  Windows machine. See [Build systems](/guide/build-systems/).
- **You need builds to happen without you.** Tag pushes, nightly builds,
  contributors' pull requests. atom runs when you run it.
- **You are shipping from a machine you do not control**, or want the release
  provenance a hosted runner gives you.

> [!TIP]
> These are not exclusive. A common arrangement is atom for everyday local
> releases and a small CI job for the one target that needs a real machine —
> the config for both is short precisely because each is doing less.

## Using atom inside CI

Nothing stops you. atom is a single binary with no dependencies, and running it
in a workflow collapses the matrix into one job:

```yaml
jobs:
  release:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - run: |
          git clone --depth 1 https://github.com/yavuzselimsahin/atom-builder /tmp/atom
          make -C /tmp/atom
      - uses: goto-bus-stop/setup-zig@v2
      - run: /tmp/atom/atom release
      - run: /tmp/atom/atom publish -y
        env:
          GITHUB_TOKEN: ${{ secrets.GITHUB_TOKEN }}
```

One runner instead of five, and the same manifest you use locally — so a
release built in CI and one built on your laptop come out of the same
description rather than two that have to be kept in sync.
