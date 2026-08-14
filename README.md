# atom-builder

Cross-platform builds for native projects, from one machine, in seconds.

Point `atom` at a project that already builds with `make` and it produces a
binary for every target you declare — no CI, no containers, no VMs.

```
$ atom build
atomik-ssg 0.3.0 — 5 targets

  ok    linux-x86_64         5.30s    473 KB  static ELF x86_64
  ok    linux-arm64          5.32s    462 KB  static ELF aarch64
  ok    macos-arm64          5.28s    242 KB  Mach-O aarch64
  ok    macos-x86_64         7.02s    204 KB  Mach-O x86_64
  ok    windows-x86_64       5.48s    260 KB  PE x86_64

5/5 succeeded in 7.19s → ./dist/
```

## Why

`goreleaser` is Go-centric, `cargo-dist` is Rust-centric. For C, C++ and Zig
projects there is no equivalent, so everyone hand-writes the same build matrix
in every repository's CI config and waits minutes for runners to spin up.

atom does the same work locally in seconds, because cross-compiling a program
is much cheaper than emulating a machine to compile it on.

## It does not replace your build system

atom invokes `make` with the right variables. It generates no Dockerfiles,
requires no CMake, and imposes no DSL. A Makefile that respects `CC ?= cc` is
already compatible — including the one you already have.

```
make -j4 CC="zig cc -target aarch64-linux-musl" LDFLAGS=-s
```

That is the whole mechanism. atom's job is to run it once per target, in
isolation, in parallel, and to check the result.

## Install

atom itself has no dependencies beyond libc. It needs whichever toolchain your
project uses, plus [zig](https://ziglang.org) on `PATH` when a target is
cross-compiled with the default `zig` strategy.

```sh
git clone https://github.com/yavuzselimsahin/atom-builder.git
cd atom-builder
make
make install PREFIX=~/.local     # or: sudo make install
```

`~/.local/bin` is already on `PATH` on most systems and needs no sudo. If you
are working on atom itself, `make link` symlinks instead of copying, so a
rebuild takes effect without a second step. `make uninstall` removes it.

Runs on macOS, Linux and the BSDs. Windows is a supported *target*, not yet a
supported *host*.

## Usage

Start in your project directory:

```
$ atom init
wrote ./atom.toml

  detected make
  5 targets, project name "atomik-ssg"
  artifact "atomik-ssg", read from the Makefile

Next: check it over, then run `atom targets`.
```

`init` reads the directory rather than printing a template: `Cargo.toml`,
`go.mod`, `build.zig`, a `.csproj` or a `Makefile` each pick the build system,
the triples are spelled the way that system expects them, and a Makefile is
scanned for the name of the binary it produces. It never overwrites an existing
`atom.toml`.

Or write one by hand:

```toml
[project]
name    = "atomik-ssg"
version = "0.3.0"

[build]
artifact = "atomik-ssg"     # the file your build produces
strip    = true             # 5x smaller static binaries

[target.linux-x86_64]
triple = "x86_64-linux-musl"

[target.linux-arm64]
triple = "aarch64-linux-musl"

[target.macos-arm64]
triple = "aarch64-macos"

[target.windows-x86_64]
triple   = "x86_64-windows-gnu"
artifact = "atomik-ssg.exe"
make     = "BIN=atomik-ssg.exe LIBS=-lws2_32"
```

Then:

```sh
atom init               # write a starting atom.toml for this project
atom targets            # show what would be built
atom build              # build everything
atom verify             # run each binary, emulating where needed
atom package            # archive it and write SHA256SUMS
atom release            # build, verify, then package
atom build -t linux-arm64
atom build -v           # show build output even on success
atom build --no-cache   # rebuild even when nothing changed
```

Artifacts land in `dist/<target>/`. Each target is built in its own tree under
`build/<target>/`.

## Packaging

```toml
[package]
include  = "README.md LICENSE"
checksum = true
```

```
$ atom release
...
  ok    atomik-ssg-0.3.0-linux-arm64.tar.gz     227 KB  3 files
  ok    atomik-ssg-0.3.0-windows-x86_64.zip     130 KB  3 files
  ok    SHA256SUMS                                      sha256
```

Unix targets get `.tar.gz`, Windows targets get `.zip`, and each archive holds
a single `<name>-<version>-<target>/` directory. `SHA256SUMS` is written in the
format `sha256sum -c` reads.

`atom release` packages only a build in which every target succeeded — a
partial set of archives is worse than none, because it looks complete.

Both formats are written directly by atom, and compressed with `gzip(1)`. No
compression library is linked in.

## Publishing

```toml
[publish.ssh]
host = "user@vps"
path = "/var/www/dl/atomik-ssg"

[publish.github]
repo = "yavuzselimsahin/atomik-ssg"
tag  = "v0.3.0"                     # defaults to v<version>
```

```sh
atom publish --dry-run   # show what would be uploaded, upload nothing
atom publish             # asks before uploading
atom publish -y          # for scripts
```

Both destinations run if both are configured. `atom publish` uploads every
archive plus `SHA256SUMS`.

Omit `host` and `path` is treated as a local directory — useful for a mounted
volume, and it makes the upload path testable without a server.

GitHub publishing reads `GITHUB_TOKEN` or `GH_TOKEN` from the environment. The
token is handed to `curl` on stdin via `--config -`, so it never appears in
`argv` where `ps` would expose it to every user on the machine, and it is never
written to disk.

> The GitHub path has not yet been exercised against the live API — see
> [ROADMAP.md](ROADMAP.md). The rsync path is tested end to end.

### Manifest reference

| Key | Default | Meaning |
|---|---|---|
| `project.name` | required | Project name |
| `project.version` | `0.0.0` | Version |
| `build.system` | `make` | `make`, `cargo`, `go`, `zig`, `dotnet`, `custom` |
| `build.artifact` | required | Name of the file the build produces |
| `build.command` | from system | Overrides the system's command line |
| `build.output` | from system | Overrides where the artifact lands |
| `build.clean` | from system | Overrides the system's clean command |
| `build.strip` | `true` | Apply the system's strip flag |
| `target.<id>.triple` | required | Zig target triple |
| `target.<id>.artifact` | `build.artifact` | Per-target override |
| `target.<id>.make` | — | Extra `make` variables for this target |
| `target.<id>.strategy` | `zig` | `zig`, `container`, or `native` |
| `target.<id>.image` | — | Container image (required for `container`) |
| `target.<id>.setup` | — | Shell command run inside the container first |
| `target.<id>.format` | auto | `tar.gz` or `zip` for this target |
| `target.<id>.verify` | `false` | Insist this target be verified |
| `verify.expect` | — | Substring the output must contain |
| `verify.args` | — | Arguments passed to the binary |
| `verify.exit` | `0` | Expected exit status |
| `verify.timeout` | `60` | Seconds before the run is killed |
| `verify.image` | `alpine:3.20` | Container for emulated Linux runs |
| `package.format` | `tar.gz` | Default archive format |
| `package.include` | — | Extra files, space separated |
| `package.checksum` | `true` | Write `SHA256SUMS` |
| `publish.ssh.host` | — | `user@host`; omit for a local path |
| `publish.ssh.path` | — | Destination directory |
| `publish.github.repo` | — | `owner/name` |
| `publish.github.tag` | `v<version>` | Release tag |

Prefer `-musl` triples for anything you intend to distribute: they link
statically and depend on nothing at runtime. `-gnu` triples link dynamically.

## Caching

Builds are skipped when nothing that matters has changed:

```
$ atom build
  ok    linux-arm64        cached    462 KB  static ELF aarch64
```

The key covers the content of every source file, the exact command that would
run, and the toolchain version — not timestamps, so `touch` does not trigger a
rebuild. Change a byte, a build flag, or the zig version and the target
rebuilds. `--no-cache` forces one regardless.

A cache hit is still checked against the target's expected architecture, so a
corrupted cache cannot smuggle the wrong binary into a release.

## Build systems

atom is not tied to `make`. The build system is one line, and everything
downstream — isolation, parallelism, artifact verification, caching, packaging,
publishing — never learns which one it is driving.

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

| System | Invocation | Artifact lands in |
|---|---|---|
| `make` | `make -j{jobs} CC="zig cc -target …"` | the tree root |
| `cargo` | `cargo build --release --target {triple}` | `target/{triple}/release/` |
| `go` | `go build` with `GOOS`/`GOARCH` set | wherever `-o` says |
| `zig` | `zig build -Dtarget={triple}` | `zig-out/bin/` |
| `dotnet` | `dotnet publish -r {rid} --self-contained` | the publish directory |
| `custom` | whatever you write | wherever you say |

`custom` is the escape hatch:

```toml
[build]
system   = "custom"
command  = "./build.sh {triple}"
output   = "out/{triple}/hello"
artifact = "hello"
```

Placeholders available in `command`, `output` and `clean`: `{triple}`,
`{artifact}`, `{name}`, `{version}`, `{jobs}`, `{os}`, `{arch}`, `{goos}`,
`{goarch}`, `{rid}`. A placeholder atom does not recognise fails the build
rather than reaching a compiler as a literal brace.

Setting `command` replaces the system's whole command line, including its
`-j{jobs}` — you own what you write.

> `make`, `zig`, `dotnet` and `custom` are exercised against real projects in
> the test suite. `cargo` and `go` are written from those tools' documented
> interfaces but not run here, because neither toolchain is installed on the
> development machine — see [ROADMAP.md](ROADMAP.md).

## Build strategies

Separate from *what* builds, `strategy` decides *how* the compiler is obtained.
By default a target is cross-compiled with `zig cc`. Some builds need the
target's real userland instead — for distribution packages, or for configure
scripts that run the binaries they just built:

```toml
[target.alpine-arm64]
strategy = "container"
triple   = "aarch64-linux-musl"
image    = "alpine:3.20"
setup    = "apk add --no-cache build-base"   # runs inside the container
```

```
  ok    linux-arm64     2.34s   462 KB  static ELF aarch64    <- zig
  ok    alpine-arm64    2.56s   259 KB  dynamic ELF aarch64   <- container
```

The zig build is static; the container build links against Alpine's own musl.
Docker provides the emulation, so a `linux/amd64` target builds on Apple
silicon with no qemu setup.

| Strategy | Meaning |
|---|---|
| `zig` | cross-compile with `zig cc` (default) |
| `container` | build inside `image`, in the target's userland |
| `native` | build with the host's own `cc`, no target flags |

## Verification

`atom build` proves the right thing was *compiled*. `atom verify` proves it
*starts* — the one thing cross-compilation cannot tell you on its own.

```toml
[verify]
expect  = "static site generator"   # substring the output must contain
args    = ""                        # arguments to pass; default none
exit    = 0                         # expected status
timeout = 60                        # seconds before the run is killed
image   = "alpine:3.20"             # container used for emulated Linux runs
```

```
$ atom verify
  ok    linux-x86_64         8.85s  ran under docker
  ok    linux-arm64          3.78s  ran under docker
  ok    macos-arm64          0.01s  ran under host
  ok    macos-x86_64         1.86s  ran under host
  skip  windows-x86_64              no wine on this host
```

atom picks the cheapest runner that works: directly on the host where the OS
and architecture allow it (including x86_64 on Apple silicon, via Rosetta),
`qemu-<arch>` on a Linux host, and a Linux container otherwise — `qemu-user`
translates Linux syscalls, so it needs a Linux host, and on macOS Docker's VM
provides one. `dist/` is mounted read-only.

Checking the status code alone is not enough: plenty of programs exit 0 while
doing nothing useful, which is what `expect` is for.

A target with no available runner is **skipped**, not failed — a machine
without Docker should still be able to build. Set `verify = true` on a target
to insist it be checked, which turns a skip into a failure.

## An exit code is not proof

A build can exit 0 and still be wrong. If a source tree carries object files
from an earlier build for another architecture, `make` may decide there is
nothing to do and leave the previous binary in place — a plausible-looking
artifact of entirely the wrong platform.

atom defends against this twice. Every target is built in its own isolated
tree, cloned fresh and cleaned before the compiler runs. And every artifact is
identified by reading its own ELF, Mach-O or PE header and compared against
what the target asked for:

```
  FAIL  linux-arm64   wrong artifact: expected ELF aarch64, got Mach-O aarch64
```

## Tests

```sh
make test
```

## License

See [LICENSE](LICENSE).
