---
title: Guide
description: How atom works, and how to make it fit your project
order: 4
---

The [Quickstart](/quickstart/) gets a project shipping. This section explains
what each stage is actually doing, and how to change it when the defaults do
not fit.

## The idea in one paragraph

Cross-compiling a program is far cheaper than emulating a machine to compile it
on. Given a toolchain that can target other platforms — which zig, cargo, go
and .NET all have — one laptop can produce binaries for every platform you ship
to, in seconds, with no CI and no containers. What has been missing is the
unglamorous work around that: isolating builds so they cannot corrupt each
other, checking that what came out is what was asked for, archiving, checksums,
and getting the result somewhere people can download it. That is atom.

If you are arriving from a CI matrix, [Coming from CI](/vs-actions/) puts the
two side by side first.

## The pipeline

```
manifest → plan → BUILD ──→ VERIFY ──→ PACKAGE ──→ PUBLISH
```

Each stage is a command of its own, and `atom release` chains the first three.
The order matters: verification sits before packaging so that a binary which
fails to start is never archived.

## Two knobs that sound alike

Two settings decide how a target gets built, and they are easy to confuse
because both sound like "how do I build this".

<table>
<thead><tr><th>Setting</th><th>Scope</th><th>Answers</th></tr></thead>
<tbody>
<tr><td><code>[build] system</code></td><td>whole project</td><td><em>What</em> builds it — make, cargo, go, zig, dotnet, or your own script</td></tr>
<tr><td><code>[target.…] strategy</code></td><td>per target</td><td><em>How</em> the compiler is obtained — cross-compile, a container, or the host's own</td></tr>
</tbody>
</table>

Most projects set neither: the default is `make`, cross-compiled with zig.

## What atom will not do

It does not replace your build system. There is no Dockerfile to generate, no
CMake to migrate to, no DSL to learn. atom invokes the build you already have
with the right arguments, collects what falls out, and checks it. A Makefile
that respects `CC ?= cc` is already compatible.

It also has no service behind it. There is no account, nothing is uploaded
anywhere you did not configure, and your signing credentials — when that lands
— stay on your machine.

## In this section

- [The manifest](/guide/manifest/) — the shape of `atom.toml`
- [Build systems](/guide/build-systems/) — make, cargo, go, zig, dotnet, custom
- [Build strategies](/guide/strategies/) — cross-compiling, containers, native
- [Verification](/guide/verification/) — proving the binaries run
- [Caching](/guide/caching/) — why the second build takes no time
- [Packaging](/guide/packaging/) — archives and checksums
- [Publishing](/guide/publishing/) — GitHub Releases and your own server
