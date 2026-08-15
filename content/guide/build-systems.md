---
title: Build systems
description: make, cargo, go, zig, dotnet, or your own script
order: 2
---

atom is not tied to `make`. One line says what builds your project, and
everything downstream — isolation, parallelism, artifact checking, caching,
packaging, publishing — never learns which one it is driving.

```toml
[build]
system   = "dotnet"
artifact = "hello"
```

```
$ atom build
  ok    macos-arm64          4.03s   70.1 MB  Mach-O aarch64
  ok    linux-x86_64         7.53s   63.7 MB  dynamic ELF x86_64
  ok    windows-x86_64       5.84s   64.4 MB  PE x86_64
```

That is a C# project cross-compiled to three platforms from one Mac, in eight
seconds.

## The systems

<table>
<thead>
<tr><th>System</th><th>Invoked as</th><th>Artifact lands in</th></tr>
</thead>
<tbody>
<tr>
  <td><code>make</code></td>
  <td><code>make -j4 CC="zig cc -target …"</code></td>
  <td>the tree root</td>
</tr>
<tr>
  <td><code>cargo</code></td>
  <td><code>cargo build --release --target …</code></td>
  <td><code>target/&lt;triple&gt;/release/</code></td>
</tr>
<tr>
  <td><code>go</code></td>
  <td><code>go build</code> with <code>GOOS</code>/<code>GOARCH</code> set</td>
  <td>wherever <code>-o</code> says</td>
</tr>
<tr>
  <td><code>zig</code></td>
  <td><code>zig build -Dtarget=…</code></td>
  <td><code>zig-out/bin/</code></td>
</tr>
<tr>
  <td><code>dotnet</code></td>
  <td><code>dotnet publish -r &lt;rid&gt; --self-contained</code></td>
  <td>the publish directory</td>
</tr>
<tr>
  <td><code>custom</code></td>
  <td>whatever you write</td>
  <td>wherever you say</td>
</tr>
</tbody>
</table>

`atom init` picks one by looking at your directory, so most projects never set
this by hand.

## custom

When atom does not know your build system, tell it directly. Two extra keys are
required, because atom has nothing to guess from:

```toml
[build]
system   = "custom"
command  = "./build.sh {triple}"
output   = "out/{triple}/myapp"
artifact = "myapp"
```

`command` is run in a clean copy of your source tree. `output` says where to
find the result, relative to that tree. `artifact` is the name it should have
in `dist/`.

The command is split into arguments on whitespace, honouring quotes, and run
directly — there is no shell, so nothing in it is interpreted. Quote an
argument that legitimately contains spaces.

## Placeholders

Every toolchain spells the same platform differently. Rather than making you
know all of them, the triple is the single source of truth and the rest are
derived.

<table>
<thead><tr><th>Placeholder</th><th>Example</th></tr></thead>
<tbody>
<tr><td><code>{triple}</code></td><td><code>aarch64-linux-musl</code></td></tr>
<tr><td><code>{artifact}</code></td><td><code>myapp</code></td></tr>
<tr><td><code>{name}</code>, <code>{version}</code></td><td><code>myapp</code>, <code>1.2.0</code></td></tr>
<tr><td><code>{jobs}</code></td><td><code>4</code></td></tr>
<tr><td><code>{os}</code>, <code>{arch}</code></td><td><code>linux</code>, <code>aarch64</code></td></tr>
<tr><td><code>{goos}</code>, <code>{goarch}</code></td><td><code>linux</code>, <code>arm64</code></td></tr>
<tr><td><code>{rid}</code></td><td><code>linux-musl-arm64</code></td></tr>
</tbody>
</table>

They work in `command`, `output` and `clean`. A placeholder atom does not
recognise fails the build immediately rather than reaching your compiler as a
literal brace.

## Overriding a system's defaults

Any of the three templates can be replaced:

```toml
[build]
system  = "make"
command = "make -j8 release"
```

> [!WARNING]
> Setting `command` replaces the system's whole command line, including the
> `-j{jobs}` it would have added — so a project that overrides it silently
> loses parallelism. If you only want to *add* arguments, use
> `[target.…] make`, which appends.

## A note on each system

**make** — the compiler arrives as `CC="zig cc -target <triple>"`, one argument
however many spaces it contains. Your Makefile needs `CC ?= cc` rather than
`CC = cc` for that to take effect.

**cargo** — stripping is controlled by the `[profile.release]` section of your
`Cargo.toml`, not by atom, so `strip = true` in the manifest does nothing here.
The target must be installed: `rustup target add aarch64-unknown-linux-musl`.

**go** — the target is selected through the environment, and `CGO_ENABLED=0` is
set so the result is a static binary that cross-compiles cleanly.

**zig** — the optimisation mode is `build.zig`'s decision, not atom's. Add
`-Doptimize=ReleaseSafe` through `[target.…] make` if you want it, or handle it
in your build script.

**dotnet** — publishes self-contained and single-file. Both matter: a plain
self-contained publish is a *directory* of runtime assemblies, and atom ships
files. Expect archives around 60 MB, since the .NET runtime is inside.

## Two things that will bite you with .NET

> [!CAUTION]
> .NET distinguishes musl from glibc in its runtime identifier. A `-musl`
> triple produces a musl build and a `-gnu` triple a glibc one; running the
> wrong one against the wrong image dies at startup. If you build `-gnu`, set
> the verification image to match:

```toml
[verify]
image = "debian:12-slim"
```

The default image is Alpine, which is musl-based — right for the static C
binaries most projects produce, wrong for a glibc-linked .NET one.

**Alpine needs more than the base image.** Even a correct musl .NET build needs
`libstdc++` and `libgcc`, which bare `alpine:3.20` does not have. Use an image
that does.

## cargo and go are untested

> [!IMPORTANT]
> Both are supported and written from their documented interfaces, but neither
> toolchain was installed on the machine atom was developed on, so neither has
> actually been run. They are table entries going through the same machinery
> as the four systems that *are* tested — a much smaller risk than untested
> logic, but not the same as having seen them work.

If you use either and hit something, that is worth reporting.
