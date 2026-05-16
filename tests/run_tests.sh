#!/bin/bash
# ─────────────────────────────────────────────────────
#  amalgame-image — Test Runner
#  Usage: ./tests/run_tests.sh [/path/to/amc]
#
#  Self-contained: no network, no broker, no server. Generates a
#  4×2 fixture PNG via a tiny C helper that links against the
#  vendored stb_image_write, then drives stdlib_image.am.
# ─────────────────────────────────────────────────────

set -u

if [ $# -ge 1 ]; then
    AMC="$1"
elif [ -n "${AMC:-}" ]; then
    :
elif command -v amc >/dev/null 2>&1; then
    AMC="$(command -v amc)"
else
    echo "ERROR: amc not found." >&2
    exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PKG_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
PKG_RUNTIME="$PKG_ROOT/runtime"
PKG_VENDOR="$PKG_ROOT/runtime/vendor"

AMC_DIR="$(cd "$(dirname "$AMC")" && pwd)"
if [ -d "$AMC_DIR/runtime" ]; then
    AMC_RUNTIME="$AMC_DIR/runtime"
elif [ -n "${AMC_RUNTIME:-}" ]; then
    :
else
    echo "ERROR: amc runtime/ not found. Set AMC_RUNTIME=..." >&2
    exit 2
fi

BUILD_DIR="$(mktemp -d -t aimg-XXXXXX)"
trap 'rm -rf "$BUILD_DIR"' EXIT
PROJ_DIR="$BUILD_DIR/proj"
mkdir -p "$PROJ_DIR"

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[0;33m'
NC='\033[0m'
PASS=0; FAIL=0; SKIP=0

echo ""
echo "════════════════════════════════════════════"
echo "  amalgame-image — Tests"
echo "════════════════════════════════════════════"
echo "  amc:     $AMC ($("$AMC" --version 2>&1 | head -1))"
echo "  runtime: $AMC_RUNTIME"
echo ""

# ── Precompile stb_impl.c once ─────────────────────
echo "── Precompiling vendored stb_impl.c ──────"
STB_IMPL_O="$BUILD_DIR/stb_impl.o"
if ! gcc -O2 -w -c \
        -I"$AMC_RUNTIME" \
        "$PKG_VENDOR/stb_impl.c" \
        -o "$STB_IMPL_O" 2>"$BUILD_DIR/stb.log"; then
    echo -e "${RED}FAIL${NC} stb_impl.c compile:"
    cat "$BUILD_DIR/stb.log" | head -5 | sed 's/^/    /'
    exit 1
fi
echo "  stb_impl.o: $(stat -c%s "$STB_IMPL_O" 2>/dev/null || stat -f%z "$STB_IMPL_O") bytes"
echo ""

# ── Generate the 4×2 RGBA fixture PNG ──────────────
FIXTURE_PNG="/tmp/amc_img_fixture.png"
cat > "$BUILD_DIR/mkfixture.c" <<EOF
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "$PKG_VENDOR/stb_image_write.h"
int main(void) {
    unsigned char px[4*2*4] = {
        255,0,0,255,    0,255,0,255,    0,0,255,255,   255,255,255,255,
        0,0,0,255,      128,128,128,255, 255,255,255,128, 0,0,0,0,
    };
    return stbi_write_png("$FIXTURE_PNG", 4, 2, 4, px, 4*4) ? 0 : 1;
}
EOF
if ! gcc -O2 -w "$BUILD_DIR/mkfixture.c" -o "$BUILD_DIR/mkfixture" 2>/dev/null; then
    echo -e "${RED}FAIL${NC} fixture builder didn't compile" >&2
    exit 1
fi
if ! "$BUILD_DIR/mkfixture"; then
    echo -e "${RED}FAIL${NC} fixture PNG write failed" >&2
    exit 1
fi

# ── Stage a fake cache pointing at the working tree ──
FAKE_CACHE="$BUILD_DIR/cache"
PKG_GIT="github.com/amalgame-lang/amalgame-image"
PKG_TAG="${PKG_TAG:-v0.1.0}"
FAKE_SHA="deadbeefcafebabe0000000000000000000000ab"
SHORT_SHA="${FAKE_SHA:0:8}"
PKG_CACHE_DIR="$FAKE_CACHE/$PKG_GIT/${PKG_TAG}_${SHORT_SHA}"

mkdir -p "$(dirname "$PKG_CACHE_DIR")"
rm -rf "$PKG_CACHE_DIR"
ln -s "$PKG_ROOT" "$PKG_CACHE_DIR"

cat > "$PROJ_DIR/amalgame.lock" <<EOF
[[package]]
name = "amalgame-image"
git  = "$PKG_GIT"
tag  = "$PKG_TAG"
rev  = "$FAKE_SHA"
EOF

export AMALGAME_PACKAGES_DIR="$FAKE_CACHE"
echo "  cache:   $FAKE_CACHE → $PKG_ROOT"
echo ""

run_test() {
    local name="$1"
    local expected="$2"
    printf "  %-38s" "$name"
    cp "$SCRIPT_DIR/stdlib_image.am" "$PROJ_DIR/test.am"
    local out_base="$PROJ_DIR/test"
    local out
    out=$(cd "$PROJ_DIR" && "$AMC" -o test test.am 2>&1)
    if [ $? -ne 0 ]; then
        echo -e "${RED}FAIL${NC} (amc)"; echo "$out" | head -3 | sed 's/^/    /'
        FAIL=$((FAIL + 1)); return
    fi
    if [ ! -f "$out_base.c" ]; then
        echo -e "${RED}FAIL${NC} (no .c)"; FAIL=$((FAIL + 1)); return
    fi
    # Two-TU link: user .c + precompiled stb_impl.o
    gcc -O2 -w \
        -I"$AMC_RUNTIME" -I"$PKG_RUNTIME" -I"$PKG_VENDOR" \
        "$out_base.c" "$STB_IMPL_O" \
        -lgc -lm -lcurl -ldl -lpthread \
        -o "$out_base" 2>"$BUILD_DIR/link.log"
    if [ ! -x "$out_base" ]; then
        echo -e "${RED}FAIL${NC} (gcc link)"
        cat "$BUILD_DIR/link.log" | head -3 | sed 's/^/    /'
        FAIL=$((FAIL + 1)); return
    fi
    local run_output
    run_output=$("$out_base" 2>&1)
    if echo "$run_output" | grep -qF "$expected"; then
        echo -e "${GREEN}PASS${NC}"; PASS=$((PASS + 1))
    else
        echo -e "${RED}FAIL${NC}"
        echo "    expected: $expected"
        echo "    got:      $(echo "$run_output" | head -3 | tr '\n' '|')"
        FAIL=$((FAIL + 1))
    fi
}

echo "── Image ───────────────────────────────────"
run_test "load fixture"          "[PASS] load fixture"
run_test "geometry 4x2"          "[PASS] geometry 4x2"
run_test "channels 4"            "[PASS] channels 4"
run_test "row0 palette"          "[PASS] row0 palette"
run_test "out-of-bounds"         "[PASS] out-of-bounds returns 0"
run_test "SetPixel in memory"    "[PASS] SetPixel in memory"
run_test "SaveAsPng"             "[PASS] SaveAsPng"
run_test "PNG round-trip alpha"  "[PASS] PNG round-trip preserves alpha"
run_test "TGA round-trip"        "[PASS] TGA round-trip"
run_test "JPG round-trip geom"   "[PASS] JPG round-trip geometry"
run_test "Free invalidates"      "[PASS] Free invalidates handle"
run_test "Free idempotent"       "[PASS] Free is idempotent"

echo ""
echo "────────────────────────────────────────────"
echo -e "  ${GREEN}PASS: $PASS${NC}  |  ${RED}FAIL: $FAIL${NC}  |  ${YELLOW}SKIP: $SKIP${NC}"
echo "────────────────────────────────────────────"
echo ""

[ $FAIL -eq 0 ] && exit 0 || exit 1
