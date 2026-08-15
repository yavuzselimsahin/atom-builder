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
<tr><td><code>wine</code></td><td>Windows target, if wine is installed on this machine</td></tr>
<tr><td><code>docker+wine</code></td><td>Windows target with no host wine, and <code>[verify] wine_image</code> naming one</td></tr>
</tbody>
</table>

> [!NOTE]
> `qemu-user` needs a Linux host, because it translates Linux system calls. On
> macOS the equivalent is a Linux container, and Docker's own VM already has
> binfmt handlers registered. That is the only reason Docker appears here.

What that container uses is worth knowing, because it decides how much the
emulation costs. Docker Desktop on Apple silicon registers **both** Rosetta and
qemu, and picks per architecture. The same busy loop, timed three ways on an
M1:

<table>
<thead><tr><th>Platform</th><th>Time</th><th>Running under</th></tr></thead>
<tbody>
<tr><td><code>linux/arm64</code></td><td>1.11 s</td><td>nothing — Docker's VM is already ARM</td></tr>
<tr><td><code>linux/amd64</code></td><td>2.43 s</td><td>Rosetta 2</td></tr>
<tr><td><code>linux/s390x</code></td><td>8.41 s</td><td>qemu</td></tr>
</tbody>
</table>

So x86_64 Linux work on an Apple silicon Mac does not go through qemu at all —
Rosetta handles it at roughly twice native, where qemu would be closer to eight
times. On an Intel Mac, or for any architecture Rosetta does not cover, qemu is
what runs.

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

## Windows targets

Verifying a Windows binary needs wine. If this machine has it, atom uses it
directly. If not, it can run one in a container instead — but only when told
which:

```toml
[verify]
wine_image = "scottyhardy/docker-wine"
```

> [!CAUTION]
> There is deliberately no default here. A wine image is gigabytes, where the
> Linux ones are megabytes, and pulling that because Docker happened to be
> running would be exactly the kind of surprise atom tries not to be. Naming
> the image is the permission.

Without wine and without that key, Windows targets are skipped, and the skip
says how to turn the container on.

## Leaving the machine as it found it

Docker pulls an image the first time it is used and leaves it behind. Somebody
who ran one build should not later discover a two-gigabyte image they never
asked for, so atom removes the images **it** introduced:

```
removing 1 image atom pulled:
  debian:12-slim                           removed
```

An image that was already on the machine is never touched — it was not atom's
to remove, and something else is probably using it.

`--keep-images` reports them instead of removing them, which is what you want
while iterating on a manifest:

```
kept 1 pulled image:
  debian:12-slim
```

Containers are always removed, including when a build fails.

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
