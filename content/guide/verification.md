---
title: Verification
description: Proving the binaries actually run
order: 4
---

Cross-compilation has one real weakness: you end up with a binary nobody has
executed. `atom build` proves the right thing was *compiled*. `atom verify`
proves it *starts*.

```
$ atom verify
  ok    linux-x86_64         8.85s  ran under docker
  ok    linux-arm64          3.78s  ran under docker
  ok    macos-arm64          0.01s  ran under host
  ok    macos-x86_64         1.86s  ran under host
  skip  windows-x86_64              no wine on this host
```

Those numbers show the cost of emulation plainly: `linux-arm64` runs natively
inside Docker's Linux VM on an Apple silicon Mac, while `linux-x86_64` has to
be emulated.

## Configuring it

```toml
[verify]
args    = "--version"     # arguments to pass; default none
expect  = "myapp 1.2"     # substring the output must contain
exit    = 0               # expected exit status
timeout = 60              # seconds before the run is killed
image   = "alpine:3.20"   # container for emulated Linux runs
```

Every key is optional. With none of them, atom runs the binary with no
arguments and checks only that it exits 0.

## Checking the exit status is not enough

Plenty of programs exit 0 while doing nothing useful. The static site generator
atom was built for prints its usage and exits 0 even for a command it does not
recognise — a verification that only looked at the status code would happily
pass a binary that had silently become useless.

That is what `expect` is for:

```toml
[verify]
expect = "static site generator"
```

If your program has a `--version` flag that prints something distinctive, that
is usually the cheapest and most reliable check:

```toml
[verify]
args   = "--version"
expect = "1.2.0"
```

## How a runner is chosen

atom picks the cheapest thing that can execute the target.

<table>
<thead><tr><th>Runner</th><th>Chosen when</th></tr></thead>
<tbody>
<tr><td><code>host</code></td><td>Same OS and architecture — or x86_64 on Apple silicon, which Rosetta handles</td></tr>
<tr><td><code>qemu</code></td><td>Linux host, different architecture, and <code>qemu-&lt;arch&gt;</code> is on <code>PATH</code></td></tr>
<tr><td><code>docker</code></td><td>Linux target on a non-Linux host — the container brings its own emulation</td></tr>
<tr><td><code>wine</code></td><td>Windows target, if wine is installed</td></tr>
</tbody>
</table>

> [!NOTE]
> `qemu-user` needs a Linux host, because it translates Linux system calls. On
> macOS the equivalent is a Linux container, and Docker's own VM already has
> binfmt and qemu registered. That is the only reason Docker appears here.

`dist/` is mounted read-only, so a binary misbehaving under emulation cannot
alter what is about to be released.

## Skipped is not failed

A machine without Docker should still be able to build. So a target with no
available runner is reported as `skip` and the command still exits 0.

But sometimes a target *must* be checked. Mark it, and a skip becomes a
failure:

```toml
[target.linux-arm64]
triple = "aarch64-linux-musl"
verify = true
```

Without that, asking for verification would be silently ignorable, which
defeats the point of asking.

Run `atom verify -v` to see why each skipped target was skipped, and to see the
output of anything that failed.

## Matching the libc

The verification image has to be able to run your binary. The default,
`alpine:3.20`, is musl-based — right for the static musl binaries most C
projects produce, wrong for anything linked against glibc.

> [!WARNING]
> If your triple ends in `-gnu`, the default musl image cannot run your binary.
> Use a glibc one:

```toml
[verify]
image = "debian:12-slim"
```

A mismatch shows up as an exit status in the 130s, or a list of missing shared
libraries under `-v`.

## Timeouts

A binary that hangs under emulation would otherwise hang atom with it. Each run
has a deadline — 60 seconds by default — after which the process is killed and
the target fails with `did not finish within 60s`.

Raise it for something genuinely slow, or lower it to catch hangs faster:

```toml
[verify]
timeout = 10
```

## Where it sits in the pipeline

`atom release` runs build, then verify, then package. Nothing that fails to
start is ever archived. If you want to package regardless, run the commands
separately.
