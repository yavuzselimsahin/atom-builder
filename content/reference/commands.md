---
title: Commands and flags
description: The complete command line
order: 1
---

```
atom <command> [options]
```

Every command carries its own help, which lists only the options that apply
to it:

```bash
atom --help              # the overview
atom build --help        # just build's options
atom help publish        # the same thing, spelled the other way
```

An option a command does not take is refused rather than ignored, so a
misplaced flag stops you instead of quietly doing nothing:

```
$ atom package --no-cache
atom: package does not take --no-cache
      try `atom package --help`
```

## Commands

### atom init

Writes a starting `atom.toml` for the current project.

It reads the directory rather than printing a blank template: a `Cargo.toml`,
`go.mod`, `build.zig`, `.csproj` or `Makefile` picks the build system, the
triples are spelled the way that system expects, and a Makefile is scanned for
the name of the binary it produces (`BIN`, `TARGET`, `PROG`, `EXE` or `NAME`).

Never overwrites an existing `atom.toml`. Exits 1 if one is there.

### atom targets

Prints the resolved target table — id, triple, expected format and
architecture, artifact name — and builds nothing. The quickest way to check a
manifest parses.

### atom build

Builds every target in the manifest, in parallel.

Each target gets its own clone of the source tree, cleaned before the build so
stale object files cannot link into it. Every finished artifact is identified by
reading its own header and compared against what the target asked for; a
mismatch fails the target even if the build exited 0. Targets whose inputs have
not changed are served from the cache.

Exits 1 if any target failed.

### atom verify

Runs each built binary and reports whether it starts, using emulation where
needed. Targets with no available runner are skipped, unless the target set
`verify = true`.

Exits 1 only on real failures; skips alone do not fail the command.

### atom package

Archives what `build` produced and writes `SHA256SUMS`. Unix targets get
`.tar.gz`, Windows targets `.zip`. A target that has not been built yet is an
error.

### atom publish

Uploads the archives and `SHA256SUMS` to every configured destination. Lists
what will be sent and asks for confirmation first.

### atom release

`build`, then `verify`, then `package` — each conditional on the previous stage
fully succeeding. Does not publish.

### atom version

Prints the version.

## Options

Which options a command accepts:

<table>
<thead>
<tr><th>Option</th><th>Default</th><th>Accepted by</th><th>Meaning</th></tr>
</thead>
<tbody>
<tr>
  <td><code>-f</code>, <code>--file &lt;path&gt;</code></td>
  <td><code>atom.toml</code></td>
  <td>all</td>
  <td>Manifest to read. Its directory becomes the project root unless <code>-C</code> says otherwise.</td>
</tr>
<tr>
  <td><code>-C &lt;dir&gt;</code></td>
  <td>the manifest's directory</td>
  <td>all</td>
  <td>Project root. This is how <code>init</code> is pointed somewhere else.</td>
</tr>
<tr>
  <td><code>-t</code>, <code>--target &lt;id&gt;</code></td>
  <td>all targets</td>
  <td>build, verify, package, publish, release</td>
  <td>Act on one target only. On <code>package</code> and <code>publish</code> this also drops <code>SHA256SUMS</code>, which describes a whole release.</td>
</tr>
<tr>
  <td><code>-j &lt;n&gt;</code></td>
  <td>core count</td>
  <td>build, release</td>
  <td>How many targets to build at once.</td>
</tr>
<tr>
  <td><code>--make-jobs &lt;n&gt;</code></td>
  <td><code>4</code></td>
  <td>build, release</td>
  <td>The <code>-j</code> passed to each individual build.</td>
</tr>
<tr>
  <td><code>--no-cache</code></td>
  <td>off</td>
  <td>build, release</td>
  <td>Rebuild even when nothing has changed.</td>
</tr>
<tr>
  <td><code>--dry-run</code></td>
  <td>off</td>
  <td>publish</td>
  <td>Show what would be uploaded, and upload nothing.</td>
</tr>
<tr>
  <td><code>-y</code>, <code>--yes</code></td>
  <td>off</td>
  <td>publish</td>
  <td>Do not ask for confirmation.</td>
</tr>
<tr>
  <td><code>-v</code>, <code>--verbose</code></td>
  <td>off</td>
  <td>build, verify, publish, release</td>
  <td>Show the underlying tool's own output, and explain each skipped verification.</td>
</tr>
<tr>
  <td><code>--keep-images</code></td>
  <td>off</td>
  <td>build, verify, release</td>
  <td>Leave the images atom pulled on the machine instead of removing them.</td>
</tr>
<tr>
  <td><code>-h</code>, <code>--help</code></td>
  <td></td>
  <td>all</td>
  <td>The overview, or that command's own help.</td>
</tr>
</tbody>
</table>

## Exit codes

<table>
<thead><tr><th>Code</th><th>Meaning</th></tr></thead>
<tbody>
<tr><td><code>0</code></td><td>Success.</td></tr>
<tr><td><code>1</code></td><td>The operation failed — a target did not build, a binary did not run, an upload was refused.</td></tr>
<tr><td><code>2</code></td><td>Usage error: an unknown command, an unknown flag, or a flag missing its value.</td></tr>
</tbody>
</table>

## Environment

<table>
<thead><tr><th>Variable</th><th>Used by</th></tr></thead>
<tbody>
<tr><td><code>GITHUB_TOKEN</code></td><td>GitHub publishing. Checked first.</td></tr>
<tr><td><code>GH_TOKEN</code></td><td>GitHub publishing. Fallback.</td></tr>
</tbody>
</table>

## Working from elsewhere

Both of these build the project in `../other`:

```bash
atom build -f ../other/atom.toml
atom build -C ../other
```

The manifest's own directory is the project root by default, so the first form
does what it looks like.
