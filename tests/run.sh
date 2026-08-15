#!/bin/sh
# Smoke tests for atom. Builds tiny fixture projects rather than depending on
# anything outside this repository, so the suite works from a fresh clone.
#
# Requires: zig on PATH (for the cross-compile cases).

set -u

ATOM=$(cd "$(dirname "$0")/.." && pwd)/atom
TMP=$(mktemp -d)
PASS=0
FAIL=0

cleanup() { rm -rf "$TMP"; }
trap cleanup EXIT

ok()   { PASS=$((PASS + 1)); printf '  ok   %s\n' "$1"; }
bad()  { FAIL=$((FAIL + 1)); printf '  FAIL %s\n' "$1"; [ $# -gt 1 ] && printf '       %s\n' "$2"; }

# assert_contains <name> <haystack> <needle>
assert_contains() {
    case "$2" in
        *"$3"*) ok "$1" ;;
        *)      bad "$1" "expected to find: $3" ;;
    esac
}

# assert_status <name> <expected> <actual>
assert_status() {
    if [ "$2" = "$3" ]; then ok "$1"; else bad "$1" "expected exit $2, got $3"; fi
}

# --------------------------------------------------------------------------
# A fixture project: one C file, a Makefile that honours CC, BIN and LDFLAGS.
# --------------------------------------------------------------------------
make_fixture() {
    dir="$TMP/$1"
    mkdir -p "$dir"

    cat > "$dir/hello.c" <<'EOF'
#include <stdio.h>
int main(void) { printf("hello\n"); return 0; }
EOF

    cat > "$dir/Makefile" <<'EOF'
CC      ?= cc
CFLAGS  ?= -O2
LDFLAGS ?=
BIN     := hello

all: $(BIN)

$(BIN): hello.o
	$(CC) $(LDFLAGS) -o $@ hello.o $(LIBS)

hello.o: hello.c
	$(CC) $(CFLAGS) -c hello.c -o hello.o

clean:
	rm -f hello.o hello hello.exe

.PHONY: all clean
EOF
    echo "$dir"
}

echo "atom test suite"
echo

# --------------------------------------------------------------------------
echo "help"

OUT=$("$ATOM" --help 2>&1); RC=$?
assert_status "--help succeeds" 0 $RC
assert_contains "the overview lists commands" "$OUT" "build every target"
assert_contains "it points at per-command help" "$OUT" "atom <command> --help"

OUT=$("$ATOM" build --help 2>&1); RC=$?
assert_status "a command has its own help" 0 $RC
assert_contains "it shows that command's usage" "$OUT" "usage: atom build"
assert_contains "it lists that command's flags" "$OUT" "--no-cache"
case "$OUT" in
    *"--dry-run"*) bad "build help hides publish-only flags" ;;
    *)             ok  "build help hides publish-only flags" ;;
esac

OUT=$("$ATOM" help publish 2>&1)
assert_contains "\`atom help <command>\` works too" "$OUT" "usage: atom publish"

# An option the command does not take is a mistake, not something to ignore.
OUT=$("$ATOM" package --no-cache 2>&1); RC=$?
assert_status "an inapplicable flag is refused" 2 $RC
assert_contains "it names the command and the flag" "$OUT" "package does not take --no-cache"

OUT=$("$ATOM" nosuchcommand 2>&1); RC=$?
assert_status "an unknown command is refused" 2 $RC
assert_contains "it says so" "$OUT" "no such command"

echo

# --------------------------------------------------------------------------
echo "init"

D="$TMP/init-make"; mkdir -p "$D"; touch "$D/Makefile"
OUT=$("$ATOM" init -C "$D" 2>&1); RC=$?
assert_status "init writes a manifest" 0 $RC
assert_contains "it says which system it found" "$OUT" "detected make"
[ -f "$D/atom.toml" ] && ok "atom.toml appears" || bad "atom.toml appears"

OUT=$("$ATOM" targets -f "$D/atom.toml" 2>&1); RC=$?
assert_status "the generated manifest parses" 0 $RC
assert_contains "it names the project after the directory" "$OUT" "init-make"

# Overwriting somebody's manifest is never the right default.
OUT=$("$ATOM" init -C "$D" 2>&1); RC=$?
assert_status "init refuses to overwrite" 1 $RC
assert_contains "it says nothing changed" "$OUT" "nothing was changed"

# Cargo spells triples differently, and a template using zig's spelling would
# fail on the first run.
D="$TMP/init-cargo"; mkdir -p "$D"; touch "$D/Cargo.toml" "$D/Makefile"
"$ATOM" init -C "$D" >/dev/null 2>&1
OUT=$(cat "$D/atom.toml")
assert_contains "Cargo.toml wins over a Makefile" "$OUT" 'system   = "cargo"'
assert_contains "cargo gets rust triples" "$OUT" "x86_64-unknown-linux-musl"

D="$TMP/init-go"; mkdir -p "$D"; touch "$D/go.mod"
"$ATOM" init -C "$D" >/dev/null 2>&1
assert_contains "go.mod is detected" "$(cat "$D/atom.toml")" 'system   = "go"'

D="$TMP/init-dotnet"; mkdir -p "$D"; touch "$D/app.csproj"
"$ATOM" init -C "$D" >/dev/null 2>&1
assert_contains "a csproj is detected" "$(cat "$D/atom.toml")" 'system   = "dotnet"'

# With nothing recognisable, custom is the honest answer — and it must say so
# rather than producing a manifest that quietly cannot run.
D="$TMP/init-none"; mkdir -p "$D"; touch "$D/README.md"
OUT=$("$ATOM" init -C "$D" 2>&1)
assert_contains "an unknown project falls back to custom" "$OUT" "no build system recognised"

echo

# --------------------------------------------------------------------------
# The JSON reader is exercised directly: it is the only parser facing data
# atom did not write, and its depth handling cannot be seen from the CLI.
# --------------------------------------------------------------------------
SRC=$(cd "$(dirname "$0")/.." && pwd)
if "${CC:-cc}" -O2 -Wall -Wextra -o "$TMP/json_test" \
        "$SRC/tests/json_test.c" "$SRC/src/json.c" 2>"$TMP/json_build.log"; then
    if "$TMP/json_test" > "$TMP/json.log" 2>&1; then
        sed 's/^/  /' "$TMP/json.log" | grep -c '  ok' > /dev/null
        grep '^  ok' "$TMP/json.log" | while read -r _; do :; done
        N=$(grep -c '^  ok' "$TMP/json.log")
        PASS=$((PASS + N))
        printf 'json\n  ok   %d assertions\n' "$N"
    else
        FAIL=$((FAIL + 1))
        printf 'json\n  FAIL unit tests\n'
        sed 's/^/       /' "$TMP/json.log"
    fi
else
    FAIL=$((FAIL + 1))
    printf 'json\n  FAIL could not compile the unit tests\n'
    sed 's/^/       /' "$TMP/json_build.log"
fi
echo

# --------------------------------------------------------------------------
echo "manifest"

D=$(make_fixture manifest)
cat > "$D/atom.toml" <<'EOF'
[project]
name    = "fixture"
version = "1.2.3"

[build]
artifact = "hello"

[target.linux-arm64]
triple = "aarch64-linux-musl"

[target.windows-x86_64]
triple   = "x86_64-windows-gnu"
artifact = "hello.exe"        # inline comment must not leak into the value
EOF

OUT=$("$ATOM" targets -f "$D/atom.toml" 2>&1)
assert_contains "reads project name and version" "$OUT" "fixture 1.2.3"
assert_contains "derives expected format from triple" "$OUT" "ELF aarch64"
assert_contains "derives PE from a windows triple" "$OUT" "PE x86_64"
assert_contains "strips inline comments" "$OUT" "hello.exe"

# artifact is required
cat > "$D/bad1.toml" <<'EOF'
[project]
name = "x"
[target.a]
triple = "aarch64-linux-musl"
EOF
OUT=$("$ATOM" targets -f "$D/bad1.toml" 2>&1); RC=$?
assert_status "missing artifact is rejected" 1 $RC
assert_contains "missing artifact explains itself" "$OUT" "artifact is required"

# a target needs a triple
cat > "$D/bad2.toml" <<'EOF'
[project]
name = "x"
[build]
artifact = "hello"
[target.a]
EOF
OUT=$("$ATOM" targets -f "$D/bad2.toml" 2>&1); RC=$?
assert_status "target without a triple is rejected" 1 $RC

# no targets at all
cat > "$D/bad3.toml" <<'EOF'
[project]
name = "x"
[build]
artifact = "hello"
EOF
OUT=$("$ATOM" targets -f "$D/bad3.toml" 2>&1); RC=$?
assert_status "manifest with no targets is rejected" 1 $RC
assert_contains "no targets explains itself" "$OUT" "no targets declared"

OUT=$("$ATOM" targets -f "$TMP/does-not-exist.toml" 2>&1); RC=$?
assert_status "missing manifest is rejected" 1 $RC

# --------------------------------------------------------------------------
echo
echo "build"

if ! command -v zig >/dev/null 2>&1; then
    echo "  skip build tests — zig is not on PATH"
else
    D=$(make_fixture build)
    cat > "$D/atom.toml" <<'EOF'
[project]
name    = "fixture"
version = "0.1.0"

[build]
artifact = "hello"
strip    = true

[target.linux-x86_64]
triple = "x86_64-linux-musl"

[target.linux-arm64]
triple = "aarch64-linux-musl"
EOF

    OUT=$("$ATOM" build -f "$D/atom.toml" 2>&1); RC=$?
    assert_status "cross-compiles every target" 0 $RC
    assert_contains "reports the x86_64 result" "$OUT" "static ELF x86_64"
    assert_contains "reports the aarch64 result" "$OUT" "static ELF aarch64"

    [ -f "$D/dist/linux-arm64/hello" ] \
        && ok "collects artifacts into dist/" \
        || bad "collects artifacts into dist/"

    [ -d "$D/build/linux-x86_64" ] && [ -d "$D/build/linux-arm64" ] \
        && ok "gives each target its own work tree" \
        || bad "gives each target its own work tree"

    OUT=$("$ATOM" build -f "$D/atom.toml" -t linux-arm64 2>&1)
    assert_contains "-t builds one target" "$OUT" "1 target"

    OUT=$("$ATOM" build -f "$D/atom.toml" -t nope 2>&1); RC=$?
    assert_status "-t with an unknown id fails" 1 $RC

    # ----------------------------------------------------------------------
    # The safety net: a build that exits 0 without producing the right thing.
    # `clean` is neutered so make finds the native binary up to date and does
    # nothing, leaving an artifact of the host's architecture behind.
    # ----------------------------------------------------------------------
    D=$(make_fixture stale)
    cat > "$D/atom.toml" <<'EOF'
[project]
name    = "fixture"
version = "0.1.0"

[build]
artifact = "hello"
clean    = "true"

[target.linux-arm64]
triple = "aarch64-linux-musl"
EOF
    ( cd "$D" && make >/dev/null 2>&1 )     # leave a native binary in the tree

    OUT=$("$ATOM" build -f "$D/atom.toml" 2>&1); RC=$?
    assert_status "stale artifact fails the build" 1 $RC
    assert_contains "stale artifact is named as such" "$OUT" "wrong artifact"

    # ----------------------------------------------------------------------
    echo
    echo "failure reporting"

    D=$(make_fixture broken)
    echo 'int main(void) { this is not c }' > "$D/hello.c"
    cat > "$D/atom.toml" <<'EOF'
[project]
name    = "fixture"
version = "0.1.0"

[build]
artifact = "hello"

[target.linux-arm64]
triple = "aarch64-linux-musl"
EOF

    OUT=$("$ATOM" build -f "$D/atom.toml" 2>&1); RC=$?
    assert_status "a failing compile fails the run" 1 $RC
    assert_contains "the compiler's output is shown" "$OUT" "error"

    # A generated manifest has to run green on the first try. A template that
    # needs editing before it works teaches the format by failing, which is
    # the wrong way to learn it.
    D=$(make_fixture init_e2e)
    rm -f "$D/atom.toml"
    "$ATOM" init -C "$D" >/dev/null 2>&1
    OUT=$("$ATOM" build -f "$D/atom.toml" 2>&1); RC=$?
    assert_status "an unedited generated manifest builds every target" 0 $RC
    assert_contains "including windows" "$OUT" "PE x86_64"
    assert_contains "the artifact name came from the Makefile" \
        "$(cat "$D/atom.toml")" 'artifact = "hello"' 

    # ----------------------------------------------------------------------
    echo
    echo "build systems"

    # custom: every template comes from the manifest.
    D=$(make_fixture sys_custom)
    cat > "$D/build.sh" <<'EOF'
#!/bin/sh
set -e
mkdir -p "out/$1"
zig cc -target "$1" -o "out/$1/hello" hello.c
EOF
    chmod +x "$D/build.sh"
    cat > "$D/atom.toml" <<'EOF'
[project]
name    = "fixture"
version = "0.1.0"

[build]
system   = "custom"
command  = "./build.sh {triple}"
output   = "out/{triple}/hello"
artifact = "hello"

[target.linux-arm64]
triple = "aarch64-linux-musl"
EOF
    OUT=$("$ATOM" build -f "$D/atom.toml" 2>&1); RC=$?
    assert_status "a custom system builds" 0 $RC
    assert_contains "the custom output path is honoured" "$OUT" "ELF aarch64"

    # custom without its templates cannot be resolved.
    sed '/^output /d' "$D/atom.toml" > "$D/nooutput.toml"
    OUT=$("$ATOM" targets -f "$D/nooutput.toml" 2>&1); RC=$?
    assert_status "custom without output is rejected" 1 $RC
    assert_contains "it says what custom needs" "$OUT" "command and output"

    # An unknown system is caught when the manifest loads, and the message
    # lists what is available.
    sed 's|system   = "custom"|system   = "bazel"|' "$D/atom.toml" > "$D/bazel.toml"
    OUT=$("$ATOM" targets -f "$D/bazel.toml" 2>&1); RC=$?
    assert_status "an unknown build system is rejected" 1 $RC
    assert_contains "the known systems are listed" "$OUT" "make"

    # A typo'd placeholder fails loudly rather than reaching a compiler.
    sed 's|{triple}|{tripel}|g' "$D/atom.toml" > "$D/typo.toml"
    OUT=$("$ATOM" build -f "$D/typo.toml" 2>&1); RC=$?
    assert_status "an unknown placeholder fails the build" 1 $RC
    assert_contains "the bad placeholder is quoted back" "$OUT" "tripel"

    # zig: a build.zig project, output under zig-out/bin.
    D=$(make_fixture sys_zig)
    rm -f "$D/Makefile" "$D/hello.c"
    printf 'const std = @import("std");\npub fn main() !void { std.debug.print("hello\\n", .{}); }\n' > "$D/main.zig"
    cat > "$D/build.zig" <<'EOF'
const std = @import("std");
pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});
    const exe = b.addExecutable(.{
        .name = "hello",
        .root_module = b.createModule(.{
            .root_source_file = b.path("main.zig"),
            .target = target,
            .optimize = optimize,
        }),
    });
    b.installArtifact(exe);
}
EOF
    cat > "$D/atom.toml" <<'EOF'
[project]
name    = "fixture"
version = "0.1.0"

[build]
system   = "zig"
artifact = "hello"

[target.linux-arm64]
triple = "aarch64-linux-musl"
EOF
    OUT=$("$ATOM" build -f "$D/atom.toml" 2>&1); RC=$?
    assert_status "a zig build system builds" 0 $RC
    assert_contains "zig-out is found" "$OUT" "ELF aarch64"

    # dotnet: proves a managed project cross-compiles to three platforms from
    # one machine. Slow and network-dependent, so it is opt-in.
    if command -v dotnet >/dev/null 2>&1 && [ -n "${ATOM_TEST_DOTNET:-}" ]; then
        D=$(make_fixture sys_dotnet)
        rm -f "$D/Makefile" "$D/hello.c"
        ( cd "$D" && dotnet new console -n hello -o . --force >/dev/null 2>&1 )
        cat > "$D/atom.toml" <<'EOF'
[project]
name    = "fixture"
version = "0.1.0"

[build]
system   = "dotnet"
artifact = "hello"

[target.windows-x86_64]
triple   = "x86_64-windows-gnu"
artifact = "hello.exe"
EOF
        OUT=$("$ATOM" build -f "$D/atom.toml" 2>&1); RC=$?
        assert_status "a dotnet project builds for windows" 0 $RC
        assert_contains "it produces a PE binary" "$OUT" "PE x86_64"
    else
        echo "  skip dotnet checks — set ATOM_TEST_DOTNET=1 to enable"
    fi

    # ----------------------------------------------------------------------
    echo
    echo "cache"

    D=$(make_fixture cache)
    cat > "$D/atom.toml" <<'EOF'
[project]
name    = "fixture"
version = "0.1.0"

[build]
artifact = "hello"

[target.linux-arm64]
triple = "aarch64-linux-musl"
EOF

    "$ATOM" build -f "$D/atom.toml" >/dev/null 2>&1
    OUT=$("$ATOM" build -f "$D/atom.toml" 2>&1)
    assert_contains "an unchanged tree is cached" "$OUT" "cached"

    # Timestamps are not inputs. Rebuilding because a file was touched would be
    # wrong, and it is what make itself gets wrong.
    touch "$D/hello.c"
    OUT=$("$ATOM" build -f "$D/atom.toml" 2>&1)
    assert_contains "touching a file does not invalidate" "$OUT" "cached"

    printf '\n/* changed */\n' >> "$D/hello.c"
    OUT=$("$ATOM" build -f "$D/atom.toml" 2>&1)
    case "$OUT" in
        *cached*) bad "changed content invalidates the cache" ;;
        *)        ok  "changed content invalidates the cache" ;;
    esac

    OUT=$("$ATOM" build -f "$D/atom.toml" 2>&1)
    assert_contains "the new content is cached in turn" "$OUT" "cached"

    OUT=$("$ATOM" build -f "$D/atom.toml" --no-cache 2>&1)
    case "$OUT" in
        *cached*) bad "--no-cache rebuilds" ;;
        *)        ok  "--no-cache rebuilds" ;;
    esac

    # The command is part of the key, so a changed flag must not be served
    # from a cache built with the old one.
    sed 's|triple = "aarch64-linux-musl"|triple = "aarch64-linux-musl"\nmake   = "CFLAGS=-O0"|' \
        "$D/atom.toml" > "$D/flags.toml"
    OUT=$("$ATOM" build -f "$D/flags.toml" 2>&1)
    case "$OUT" in
        *cached*) bad "a changed build flag invalidates the cache" ;;
        *)        ok  "a changed build flag invalidates the cache" ;;
    esac

    # ----------------------------------------------------------------------
    echo
    echo "container"

    if ! command -v docker >/dev/null 2>&1 || ! docker info >/dev/null 2>&1; then
        echo "  skip container tests — docker is not running"
    else
        D=$(make_fixture container)
        cat > "$D/atom.toml" <<'EOF'
[project]
name    = "fixture"
version = "0.1.0"

[build]
artifact = "hello"

[target.alpine-arm64]
strategy = "container"
triple   = "aarch64-linux-musl"
image    = "alpine:3.20"
setup    = "apk add --no-cache build-base"
EOF
        BEFORE=$(docker ps -aq | wc -l | tr -d ' ')

        OUT=$("$ATOM" build -f "$D/atom.toml" 2>&1); RC=$?
        assert_status "a container target builds" 0 $RC
        assert_contains "it produces a linux binary" "$OUT" "ELF aarch64"

        # The container compiles against its own libc, so the result is
        # dynamically linked where the zig strategy would be static.
        assert_contains "it links against the image's libc" "$OUT" "dynamic ELF"

        AFTER=$(docker ps -aq | wc -l | tr -d ' ')
        [ "$BEFORE" = "$AFTER" ] && ok "no container is left behind" \
                                 || bad "no container is left behind"

        # atom should leave the machine as it found it: an image it pulled is
        # taken back out, and one that was already there is not its to remove.
        if docker image inspect debian:12-slim >/dev/null 2>&1; then
            echo "  skip image cleanup test — debian:12-slim already present"
        else
            D2=$(make_fixture image_cleanup)
            cat > "$D2/atom.toml" <<'EOF'
[project]
name    = "fixture"
version = "0.1.0"

[build]
artifact = "hello"

[verify]
expect = "hello"
image  = "debian:12-slim"

[target.linux-x86_64]
triple = "x86_64-linux-gnu"
EOF
            "$ATOM" build -f "$D2/atom.toml" >/dev/null 2>&1
            OUT=$("$ATOM" verify -f "$D2/atom.toml" 2>&1)
            assert_contains "it says which images it removed" "$OUT" "removing 1 image"
            docker image inspect debian:12-slim >/dev/null 2>&1 \
                && bad "a pulled image is removed afterwards" \
                || ok  "a pulled image is removed afterwards"

            "$ATOM" build -f "$D2/atom.toml" >/dev/null 2>&1
            OUT=$("$ATOM" verify -f "$D2/atom.toml" --keep-images 2>&1)
            assert_contains "--keep-images keeps it" "$OUT" "kept 1 pulled image"
            docker image rm debian:12-slim >/dev/null 2>&1
        fi

        # An image that was already on the machine is never removed.
        OUT=$("$ATOM" build -f "$D/atom.toml" 2>&1)
        docker image inspect alpine:3.20 >/dev/null 2>&1 \
            && ok  "an image that was already present is left alone" \
            || bad "an image that was already present is left alone"

        # A container target with no image cannot be resolved.
        sed '/^image /d' "$D/atom.toml" > "$D/noimage.toml"
        OUT=$("$ATOM" targets -f "$D/noimage.toml" 2>&1); RC=$?
        assert_status "container strategy without an image is rejected" 1 $RC
        assert_contains "the missing image is explained" "$OUT" "needs an image"

        # An unknown strategy is caught when the manifest loads.
        sed 's|strategy = "container"|strategy = "magic"|' "$D/atom.toml" > "$D/bad.toml"
        OUT=$("$ATOM" targets -f "$D/bad.toml" 2>&1); RC=$?
        assert_status "an unknown strategy is rejected" 1 $RC
    fi

    # ----------------------------------------------------------------------
    echo
    echo "verify"

    # Verification needs a target this machine can actually execute, so the
    # host's own triple is derived rather than hard-coded.
    case "$(uname -s)" in
        Darwin) HOST_OS="macos" ;;
        Linux)  HOST_OS="linux-musl" ;;
        *)      HOST_OS="" ;;
    esac
    case "$(uname -m)" in
        arm64|aarch64) HOST_ARCH="aarch64" ;;
        x86_64|amd64)  HOST_ARCH="x86_64" ;;
        *)             HOST_ARCH="" ;;
    esac

    if [ -z "$HOST_OS" ] || [ -z "$HOST_ARCH" ]; then
        echo "  skip verify tests — unrecognised host"
    else
        HOST_TRIPLE="$HOST_ARCH-$HOST_OS"

        D=$(make_fixture verify)
        cat > "$D/atom.toml" <<EOF
[project]
name    = "fixture"
version = "0.1.0"

[build]
artifact = "hello"

[verify]
expect  = "hello"
timeout = 20

[target.host]
triple = "$HOST_TRIPLE"
EOF

        OUT=$("$ATOM" verify -f "$D/atom.toml" 2>&1); RC=$?
        assert_status "verifying before building fails" 1 $RC

        "$ATOM" build -f "$D/atom.toml" >/dev/null 2>&1
        OUT=$("$ATOM" verify -f "$D/atom.toml" 2>&1); RC=$?
        assert_status "a working binary verifies" 0 $RC
        assert_contains "it says which runner was used" "$OUT" "ran under host"

        # The expectation is checked against output, not just exit status.
        sed 's|expect  = "hello"|expect  = "NOT-IN-OUTPUT"|' "$D/atom.toml" > "$D/miss.toml"
        OUT=$("$ATOM" verify -f "$D/miss.toml" 2>&1); RC=$?
        assert_status "missing expected output fails" 1 $RC
        assert_contains "the missing string is named" "$OUT" "NOT-IN-OUTPUT"

        # A binary that exits non-zero is caught.
        D2=$(make_fixture verify_exit)
        printf '#include <stdio.h>\nint main(void){printf("hello\\n");return 3;}\n' > "$D2/hello.c"
        sed "s|\[target.host\]|[target.host]|" "$D/atom.toml" > "$D2/atom.toml"
        "$ATOM" build -f "$D2/atom.toml" >/dev/null 2>&1
        OUT=$("$ATOM" verify -f "$D2/atom.toml" 2>&1); RC=$?
        assert_status "a non-zero exit fails" 1 $RC
        assert_contains "the exit status is reported" "$OUT" "exited 3"

        # A binary that never returns is killed rather than hanging atom.
        D3=$(make_fixture verify_hang)
        printf 'int main(void){for(;;){}return 0;}\n' > "$D3/hello.c"
        cat > "$D3/atom.toml" <<EOF
[project]
name    = "fixture"
version = "0.1.0"

[build]
artifact = "hello"

[verify]
timeout = 3

[target.host]
triple = "$HOST_TRIPLE"
EOF
        "$ATOM" build -f "$D3/atom.toml" >/dev/null 2>&1
        OUT=$("$ATOM" verify -f "$D3/atom.toml" 2>&1); RC=$?
        assert_status "a hanging binary fails" 1 $RC
        assert_contains "the timeout is reported" "$OUT" "did not finish"

        # A target with no runner is skipped, and a skip is not a failure...
        D4=$(make_fixture verify_skip)
        cat > "$D4/atom.toml" <<'EOF'
[project]
name    = "fixture"
version = "0.1.0"

[build]
artifact = "hello"

[target.windows-x86_64]
triple   = "x86_64-windows-gnu"
artifact = "hello.exe"
make     = "BIN=hello.exe"
EOF
        "$ATOM" build -f "$D4/atom.toml" >/dev/null 2>&1
        if command -v wine >/dev/null 2>&1; then
            echo "  skip runner-absent tests — wine is installed"
        else
            OUT=$("$ATOM" verify -f "$D4/atom.toml" 2>&1); RC=$?
            assert_status "an unrunnable target is skipped, not failed" 0 $RC
            assert_contains "the skip explains itself" "$OUT" "no wine"
            # The container fallback exists but costs gigabytes, so it is named
            # rather than assumed — the skip should say how to turn it on.
            assert_contains "it points at the wine_image option" "$OUT" "wine_image"

            # ...unless the target asked to be verified.
            sed 's|make     = "BIN=hello.exe"|make     = "BIN=hello.exe"\nverify   = true|' \
                "$D4/atom.toml" > "$D4/must.toml"
            OUT=$("$ATOM" verify -f "$D4/must.toml" 2>&1); RC=$?
            assert_status "verify = true turns a skip into a failure" 1 $RC
        fi
    fi

    # ----------------------------------------------------------------------
    echo
    echo "package"

    D=$(make_fixture package)
    printf 'readme\n'  > "$D/README.md"
    printf 'license\n' > "$D/LICENSE"
    cat > "$D/atom.toml" <<'EOF'
[project]
name    = "fixture"
version = "0.1.0"

[build]
artifact = "hello"

[package]
include  = "README.md LICENSE"
checksum = true

[target.linux-arm64]
triple = "aarch64-linux-musl"

[target.windows-x86_64]
triple   = "x86_64-windows-gnu"
artifact = "hello.exe"
make     = "BIN=hello.exe"
EOF

    OUT=$("$ATOM" package -f "$D/atom.toml" 2>&1); RC=$?
    assert_status "packaging before building fails" 1 $RC
    assert_contains "packaging says what is missing" "$OUT" "run \`atom build\`"

    OUT=$("$ATOM" release -f "$D/atom.toml" 2>&1); RC=$?
    assert_status "release builds then packages" 0 $RC

    TGZ="$D/dist/fixture-0.1.0-linux-arm64.tar.gz"
    ZIP="$D/dist/fixture-0.1.0-windows-x86_64.zip"

    [ -f "$TGZ" ] && ok "tar.gz is written for a unix target" \
                  || bad "tar.gz is written for a unix target"
    [ -f "$ZIP" ] && ok "zip is chosen for a windows target" \
                  || bad "zip is chosen for a windows target"

    # The archives are written by hand, so the real assertion is that the
    # system's own tools accept them.
    if gzip -t "$TGZ" 2>/dev/null; then ok "gzip accepts the tarball"
    else bad "gzip accepts the tarball"; fi

    OUT=$(tar -tzf "$TGZ" 2>&1)
    assert_contains "tarball carries the binary" "$OUT" "fixture-0.1.0-linux-arm64/hello"
    assert_contains "tarball carries the extra files" "$OUT" "README.md"

    if tar -xzf "$TGZ" -O "fixture-0.1.0-linux-arm64/LICENSE" 2>/dev/null | grep -q license
    then ok "tarball contents survive a round trip"
    else bad "tarball contents survive a round trip"; fi

    if command -v unzip >/dev/null 2>&1; then
        if unzip -t "$ZIP" >/dev/null 2>&1
        then ok "unzip verifies every CRC"
        else bad "unzip verifies every CRC"; fi

        OUT=$(unzip -l "$ZIP" 2>&1)
        assert_contains "zip carries the binary" "$OUT" "hello.exe"
    else
        echo "  skip zip checks — unzip is not installed"
    fi

    # SHA256SUMS must be in the format the system checker reads.
    SUMS="$D/dist/SHA256SUMS"
    [ -f "$SUMS" ] && ok "SHA256SUMS is written" || bad "SHA256SUMS is written"

    if command -v sha256sum >/dev/null 2>&1; then
        CHECK="sha256sum -c"
    elif command -v shasum >/dev/null 2>&1; then
        CHECK="shasum -a 256 -c"
    else
        CHECK=""
    fi

    if [ -n "$CHECK" ]; then
        if ( cd "$D/dist" && $CHECK SHA256SUMS >/dev/null 2>&1 )
        then ok "checksums verify against the archives"
        else bad "checksums verify against the archives"; fi
    else
        echo "  skip checksum verification — no sha256 tool"
    fi

    # A directory in include would otherwise become a zero-byte file in the
    # archive, which is worse than an error because it looks fine.
    mkdir -p "$D/somedir" && printf 'x\n' > "$D/somedir/f.txt"
    sed 's|include  = .*|include  = "somedir"|' "$D/atom.toml" > "$D/dir.toml"
    OUT=$("$ATOM" package -f "$D/dir.toml" 2>&1); RC=$?
    assert_status "a directory in include is refused" 1 $RC
    assert_contains "it says why" "$OUT" "is a directory"

    # A missing include is a manifest error, not a silent omission.
    sed 's|include  = .*|include  = "README.md NOPE.md"|' "$D/atom.toml" > "$D/bad.toml"
    OUT=$("$ATOM" package -f "$D/bad.toml" 2>&1); RC=$?
    assert_status "a missing include fails the package" 1 $RC
    assert_contains "a missing include is named" "$OUT" "NOPE.md"

    # An invalid format is rejected up front.
    sed 's|^\[package\]|[package]\nformat = "rar"|' "$D/atom.toml" > "$D/rar.toml"
    OUT=$("$ATOM" targets -f "$D/rar.toml" 2>&1); RC=$?
    assert_status "an unknown archive format is rejected" 1 $RC

    # ----------------------------------------------------------------------
    echo
    echo "publish"

    # With no destination in the manifest there is nothing to do, and the
    # message has to say what to add.
    OUT=$("$ATOM" publish -f "$D/atom.toml" 2>&1); RC=$?
    assert_status "publishing with no destination fails" 1 $RC
    assert_contains "it names publish.ssh" "$OUT" "[publish.ssh]"
    assert_contains "it names publish.github" "$OUT" "[publish.github]"

    # An ssh destination without a host is a local path, which makes the whole
    # rsync path testable without a server.
    SERVER="$TMP/server/dl"
    cat >> "$D/atom.toml" <<EOF

[publish.ssh]
path = "$SERVER"
EOF

    OUT=$("$ATOM" publish -f "$D/atom.toml" --dry-run 2>&1); RC=$?
    assert_status "dry run succeeds" 0 $RC
    assert_contains "dry run says it would upload" "$OUT" "would upload"
    [ -d "$SERVER" ] && bad "dry run must not touch the destination" \
                     || ok "dry run leaves the destination alone"

    OUT=$(printf '\n' | "$ATOM" publish -f "$D/atom.toml" 2>&1)
    assert_contains "an empty answer cancels" "$OUT" "Cancelled"
    [ -d "$SERVER" ] && bad "cancelling must not upload" \
                     || ok "cancelling uploads nothing"

    OUT=$("$ATOM" publish -f "$D/atom.toml" -y 2>&1); RC=$?
    assert_status "publishing to a local path succeeds" 0 $RC

    [ -f "$SERVER/fixture-0.1.0-linux-arm64.tar.gz" ] \
        && ok "the tarball arrives" || bad "the tarball arrives"
    [ -f "$SERVER/SHA256SUMS" ] \
        && ok "SHA256SUMS arrives" || bad "SHA256SUMS arrives"

    if [ -n "$CHECK" ]; then
        if ( cd "$SERVER" && $CHECK SHA256SUMS >/dev/null 2>&1 )
        then ok "delivered files match their checksums"
        else bad "delivered files match their checksums"; fi
    fi

    # -t narrows the upload, and drops SHA256SUMS because it describes the
    # whole release rather than one file.
    OUT=$("$ATOM" publish -f "$D/atom.toml" --dry-run -t linux-arm64 2>&1)
    assert_contains "-t narrows the upload" "$OUT" "would upload 1 file"

    # GitHub needs a token, and says so rather than failing at the request.
    cat >> "$D/atom.toml" <<'EOF'

[publish.github]
repo = "owner/fixture"
EOF
    OUT=$(env -u GITHUB_TOKEN -u GH_TOKEN "$ATOM" publish -f "$D/atom.toml" -y 2>&1)
    assert_contains "a missing token is reported" "$OUT" "GITHUB_TOKEN"

    OUT=$("$ATOM" publish -f "$D/atom.toml" --dry-run 2>&1)
    assert_contains "dry run reaches github too" "$OUT" "github: would publish"
    assert_contains "dry run derives the tag from the version" "$OUT" "v0.1.0"

    # A malformed repo is caught when the manifest loads, not at upload time.
    sed 's|repo = "owner/fixture"|repo = "fixture"|' "$D/atom.toml" > "$D/badrepo.toml"
    OUT=$("$ATOM" targets -f "$D/badrepo.toml" 2>&1); RC=$?
    assert_status "a repo without owner/name is rejected" 1 $RC
fi

# --------------------------------------------------------------------------
echo
printf '%d passed, %d failed\n' "$PASS" "$FAIL"
[ "$FAIL" -eq 0 ]
