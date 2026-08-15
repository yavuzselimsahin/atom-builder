---
title: Packaging
description: Archives and checksums
order: 6
---

`atom package` turns what `atom build` left in `dist/` into the files people
download.

```
$ atom package
atomik-ssg 1.1.0 — packaging 5 targets

  ok    atomik-ssg-1.1.0-linux-x86_64.tar.gz    239 KB  3 files
  ok    atomik-ssg-1.1.0-linux-arm64.tar.gz     227 KB  3 files
  ok    atomik-ssg-1.1.0-macos-arm64.tar.gz      99 KB  3 files
  ok    atomik-ssg-1.1.0-macos-x86_64.tar.gz     99 KB  3 files
  ok    atomik-ssg-1.1.0-windows-x86_64.zip     130 KB  3 files
  ok    SHA256SUMS                                      sha256
```

## What ends up where

`dist/` has two layers, and only one of them is published:

```
dist/
├── linux-arm64/atomik-ssg                    ← raw binary, intermediate
├── macos-arm64/atomik-ssg                    ← raw binary, intermediate
├── atomik-ssg-1.1.0-linux-arm64.tar.gz       ← published
├── atomik-ssg-1.1.0-macos-arm64.tar.gz       ← published
└── SHA256SUMS                                ← published
```

The per-target directories are where `build` puts the binaries. The archives at
the top level are what `publish` uploads.

Each archive holds a single directory named after the release, so extracting
one does not scatter files into the user's current directory:

```
atomik-ssg-1.1.0-linux-arm64/
├── atomik-ssg
├── README.md
└── LICENSE
```

## Configuration

```toml
[package]
format   = "tar.gz"                # default for non-Windows targets
include  = "README.md LICENSE"     # extra files, space separated
checksum = true                    # write SHA256SUMS
```

Windows targets become `.zip` automatically, because that is what Windows users
expect. Override per target if you need to:

```toml
[target.windows-x86_64]
format = "tar.gz"
```

`include` paths are relative to your project root. A file named there that does
not exist fails the packaging step rather than being quietly left out — a
release missing its licence is worth stopping for.

## Checksums

`SHA256SUMS` is written in the format the standard tools read:

```bash
cd dist
shasum -a 256 -c SHA256SUMS      # macOS
sha256sum -c SHA256SUMS          # Linux
```

It covers every archive in the release. Because it describes the release as a
whole, `atom package -t <one-target>` does not write one.

## Reproducibility

Archives are built without embedded timestamps or usernames, so the same inputs
produce the same bytes. That means a checksum you publish today can be
reproduced by someone rebuilding from source later.

## No compression library

Both formats are written by atom directly, and the compression comes from
`gzip(1)`. Nothing is linked in, which is part of why atom itself
cross-compiles as easily as it asks your project to.

Because the archive writers are hand-written, the project's test suite does not
take atom's word for it: `gzip -t` and `tar -tzf` have to accept the tarballs,
`unzip -t` has to verify every CRC in the zips, and extracted files have to
come back byte-for-byte identical with their permissions intact.

## Version bumps leave the old files

> [!WARNING]
> atom does not delete archives from previous versions — a tool that removes
> files from a release directory on its own is a tool that will one day remove
> the wrong one. After changing `version`, clear it yourself:

```bash
rm -rf dist && atom release
```

Otherwise `dist/` accumulates both versions. `SHA256SUMS` only ever lists the
current one, and `publish` only uploads the current one, but the stray files
are confusing.
