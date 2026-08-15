---
title: Reference
description: Every command, flag and manifest key
order: 4
---

The complete surface, for looking things up rather than reading through.

- [Commands and flags](/reference/commands/) — every command, every option,
  exit codes and environment variables
- [Manifest keys](/reference/manifest-keys/) — every key in `atom.toml` with
  its default

## Layout on disk

atom writes to two directories inside your project, both safe to delete and
both worth adding to `.gitignore`:

```
your-project/
├── atom.toml
├── build/
│   ├── .cache/            content-addressed build cache
│   ├── linux-arm64/       isolated copy of your tree, per target
│   └── macos-arm64/
└── dist/
    ├── linux-arm64/       the raw binary for that target
    ├── macos-arm64/
    ├── name-1.0-linux-arm64.tar.gz
    └── SHA256SUMS
```

```
build/
dist/
```

## Version

This documentation describes **atom 0.1.0**.
