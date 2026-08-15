---
title: Manifest keys
description: Every key in atom.toml, with its default
order: 2
---

## [project]

<table>
<thead><tr><th>Key</th><th>Default</th><th>Meaning</th></tr></thead>
<tbody>
<tr><td><code>name</code></td><td><strong>required</strong></td><td>Project name. Used in archive filenames.</td></tr>
<tr><td><code>version</code></td><td><code>0.0.0</code></td><td>Used in archive filenames and as the default release tag.</td></tr>
</tbody>
</table>

## [build]

<table>
<thead><tr><th>Key</th><th>Default</th><th>Meaning</th></tr></thead>
<tbody>
<tr><td><code>system</code></td><td><code>make</code></td><td><code>make</code>, <code>cargo</code>, <code>go</code>, <code>zig</code>, <code>dotnet</code> or <code>custom</code>.</td></tr>
<tr><td><code>artifact</code></td><td><strong>required</strong></td><td>The <em>name</em> of the file your build produces.</td></tr>
<tr><td><code>command</code></td><td>from the system</td><td>Replaces the system's whole command line, including its <code>-j{jobs}</code>.</td></tr>
<tr><td><code>output</code></td><td>from the system</td><td>Where the artifact lands, relative to the tree root.</td></tr>
<tr><td><code>clean</code></td><td>from the system</td><td>Run before every build. Use <code>"true"</code> to disable — an empty string falls back to the default.</td></tr>
<tr><td><code>strip</code></td><td><code>true</code></td><td>Apply the system's strip flag. Roughly a five-fold size reduction on static binaries.</td></tr>
</tbody>
</table>

`command` and `output` are **required** when `system = "custom"`.

## [target.&lt;id&gt;]

One section per target. The id after `target.` names it everywhere: `-t`, the
`dist/` subdirectory, the archive filename.

<table>
<thead><tr><th>Key</th><th>Default</th><th>Meaning</th></tr></thead>
<tbody>
<tr><td><code>triple</code></td><td><strong>required</strong></td><td>The target platform. Everything else is derived from it.</td></tr>
<tr><td><code>artifact</code></td><td><code>build.artifact</code></td><td>Per-target output name, e.g. <code>myapp.exe</code>.</td></tr>
<tr><td><code>make</code></td><td>—</td><td>Extra arguments appended to the build command. Works for any build system despite the name.</td></tr>
<tr><td><code>strategy</code></td><td><code>zig</code></td><td><code>zig</code>, <code>container</code> or <code>native</code>.</td></tr>
<tr><td><code>image</code></td><td>—</td><td>Container image. <strong>Required</strong> when <code>strategy = "container"</code>.</td></tr>
<tr><td><code>setup</code></td><td>—</td><td>Shell command run inside the container before the build.</td></tr>
<tr><td><code>format</code></td><td>auto</td><td><code>tar.gz</code> or <code>zip</code>. Windows triples default to zip.</td></tr>
<tr><td><code>verify</code></td><td><code>false</code></td><td>Insist this target be verified — a skip becomes a failure.</td></tr>
</tbody>
</table>

## [verify]

<table>
<thead><tr><th>Key</th><th>Default</th><th>Meaning</th></tr></thead>
<tbody>
<tr><td><code>args</code></td><td>—</td><td>Arguments passed to the built binary.</td></tr>
<tr><td><code>expect</code></td><td>—</td><td>Substring the output must contain.</td></tr>
<tr><td><code>exit</code></td><td><code>0</code></td><td>Expected exit status.</td></tr>
<tr><td><code>timeout</code></td><td><code>60</code></td><td>Seconds before the run is killed.</td></tr>
<tr><td><code>image</code></td><td><code>alpine:3.20</code></td><td>Container image for emulated Linux runs. Must match your target's libc.</td></tr>
</tbody>
</table>

## [package]

<table>
<thead><tr><th>Key</th><th>Default</th><th>Meaning</th></tr></thead>
<tbody>
<tr><td><code>format</code></td><td><code>tar.gz</code></td><td>Default archive format. Windows targets override to zip.</td></tr>
<tr><td><code>include</code></td><td>—</td><td>Extra files in every archive, space separated, relative to the project root.</td></tr>
<tr><td><code>checksum</code></td><td><code>true</code></td><td>Write <code>SHA256SUMS</code>.</td></tr>
</tbody>
</table>

## [publish.ssh]

<table>
<thead><tr><th>Key</th><th>Default</th><th>Meaning</th></tr></thead>
<tbody>
<tr><td><code>host</code></td><td>—</td><td><code>user@host</code>. Omit and <code>path</code> is a local directory.</td></tr>
<tr><td><code>path</code></td><td>—</td><td>Destination directory. Created if missing.</td></tr>
</tbody>
</table>

## [publish.github]

<table>
<thead><tr><th>Key</th><th>Default</th><th>Meaning</th></tr></thead>
<tbody>
<tr><td><code>repo</code></td><td>—</td><td><code>owner/name</code>. Rejected at load time if it has no slash.</td></tr>
<tr><td><code>tag</code></td><td><code>v&lt;version&gt;</code></td><td>Release tag.</td></tr>
</tbody>
</table>

## Placeholders

Available in `command`, `output` and `clean`.

<table>
<thead><tr><th>Placeholder</th><th>Example</th><th>From</th></tr></thead>
<tbody>
<tr><td><code>{triple}</code></td><td><code>aarch64-linux-musl</code></td><td>the target</td></tr>
<tr><td><code>{artifact}</code></td><td><code>myapp</code></td><td>the target, or <code>[build]</code></td></tr>
<tr><td><code>{name}</code></td><td><code>myapp</code></td><td><code>[project]</code></td></tr>
<tr><td><code>{version}</code></td><td><code>1.2.0</code></td><td><code>[project]</code></td></tr>
<tr><td><code>{jobs}</code></td><td><code>4</code></td><td><code>--make-jobs</code></td></tr>
<tr><td><code>{os}</code></td><td><code>linux</code></td><td>derived from the triple</td></tr>
<tr><td><code>{arch}</code></td><td><code>aarch64</code></td><td>derived from the triple</td></tr>
<tr><td><code>{goos}</code></td><td><code>linux</code></td><td>Go's spelling</td></tr>
<tr><td><code>{goarch}</code></td><td><code>arm64</code></td><td>Go's spelling</td></tr>
<tr><td><code>{rid}</code></td><td><code>linux-musl-arm64</code></td><td>.NET's spelling</td></tr>
</tbody>
</table>

An unrecognised placeholder fails the build rather than reaching your compiler
as a literal brace.

## Parser behaviour

atom reads a small, deliberate subset of TOML.

- **No arrays of tables.** Write `[target.linux-arm64]`, not `[[target]]`.
- **An empty value means unset** and falls back to the default. Disable a
  command with `"true"`, not `""`.
- **Inline comments are stripped** when they are outside quotes.
- A manifest larger than 256 keys is rejected rather than silently truncated.
