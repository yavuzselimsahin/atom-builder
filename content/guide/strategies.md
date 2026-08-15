---
title: Build strategies
description: Cross-compiling, containers, and the host's own compiler
order: 3
---

Separate from *what* builds your project, a strategy decides *how* the compiler
is obtained for one target. It is set per target, and most targets never set it.

<table>
<thead><tr><th>Strategy</th><th>What happens</th><th>Cost per target</th></tr></thead>
<tbody>
<tr><td><code>zig</code></td><td>Cross-compiled with <code>zig cc</code>. The default.</td><td>seconds</td></tr>
<tr><td><code>container</code></td><td>Built inside <code>image</code>, in the target's own userland.</td><td>minutes</td></tr>
<tr><td><code>native</code></td><td>Built with the host's own <code>cc</code>, no target flags.</td><td>seconds</td></tr>
</tbody>
</table>

## Why the default is cross-compilation

Emulating a whole machine to compile a program runs roughly an order of
magnitude slower than compiling it natively, and adds image downloads, boot
time and VM lifecycle management on top. For a project whose dependencies are
just libc — which describes most C and Rust and Go programs — none of that buys
anything the cross-compiler cannot already do.

So atom cross-compiles by default and treats emulation as a fallback for the
cases that genuinely need it, plus a [verification](/guide/verification/) layer
for the cases that do not.

## When a container earns its cost

Cross-compilation links against the toolchain's idea of the target. Sometimes
you need the target's *actual* userland:

- Building distribution packages that must link against that distro's libraries
- `configure` scripts that compile and then **run** small test programs
- Anything whose build inspects the system it is running on

```toml
[target.alpine-arm64]
strategy = "container"
triple   = "aarch64-linux-musl"
image    = "alpine:3.20"
setup    = "apk add --no-cache build-base"
```

The difference shows up in the output, and it is the whole justification:

```
  ok    linux-arm64     2.34s   462 KB  static ELF aarch64    <- zig
  ok    alpine-arm64    2.56s   259 KB  dynamic ELF aarch64   <- container
```

The zig build is static and self-contained. The container build links against
Alpine's actual musl, which is what a distribution package should do.

Docker supplies the emulation, so a `linux/amd64` target builds on Apple
silicon with no qemu setup at all.

## The image must have a toolchain

> [!NOTE]
> atom does not install compilers into your image. Either use one that already
> has them — `gcc:13`, `rust:1.80`, `golang:1.23`, or your own — or install
> them with `setup`:

```toml
setup = "apk add --no-cache build-base"
```

`setup` runs inside the container before the build, in the guest's shell. It is
the one place a shell is involved anywhere in atom, and it runs your own
command in the container rather than on your machine.

The container is started once and driven with `docker exec`, so packages
`setup` installs are still there when the build runs. It is removed afterwards,
including when the build fails.

## native

`native` builds with whatever `cc` is on the host and passes no target flags at
all. It is useful for one thing: producing a build for the machine you are
sitting at, with its own system libraries, without involving zig.

```toml
[target.host]
strategy = "native"
triple   = "aarch64-macos"
```

The triple is still required, because atom checks the finished binary against
it. If you set a triple the host cannot produce, the build will be rejected —
which is the point.

## Choosing

Start with the default. Move a target to `container` when you hit one of the
specific reasons above, not preemptively: it costs minutes per target instead
of seconds, and needs Docker running.

## What is not here yet

`qemu-system` — full machine emulation — is not implemented. It is only needed
where cross-compilation *and* containers both fail, which in practice means the
BSDs: zig ships no libc for them and Docker has no FreeBSD userland.

The design is written down in the project's `ROADMAP.md`. It was left unwritten
rather than written untested, because a VM driver nobody had ever run would be
the one part of atom that had never been seen to work.
