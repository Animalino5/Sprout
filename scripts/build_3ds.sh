#!/bin/bash
# build_3ds.sh — Turn a .bas file into a .3dsx file.
#
# Usage: ./scripts/build_3ds.sh tests/samples/hello_3ds.bas
#
# What it does (4 steps):
#   1. Build sproutc (the compiler) if not already built
#   2. Compile .bas → .c   (using sproutc)
#   3. Compile .c → .o     (using devkitARM's arm-none-eabi-gcc)
#   4. Link .o → .3dsx     (using libctru's 3dsx.specs + 3dsxtool)
#
# Output: build-3ds/<name>.3dsx
#
# Requirements:
#   - devkitPro installed (DEVKITPRO and DEVKITARM env vars set)
#   - libctru package installed (dkp-pacman -S 3ds-libctru)
#   - 3dstools package installed (dkp-pacman -S 3ds-tools)
#   - GCC for building sproutc itself

set -e

# ── Validate input ─────────────────────────────────────────────────

BAS_FILE="${1:-}"
if [ -z "$BAS_FILE" ]; then
    echo "Usage: $0 <file.bas>"
    echo "Example: $0 tests/samples/hello_3ds.bas"
    exit 1
fi

if [ ! -f "$BAS_FILE" ]; then
    echo "Error: file not found: $BAS_FILE"
    exit 1
fi

# Resolve to absolute path
BAS_FILE="$(cd "$(dirname "$BAS_FILE")" && pwd)/$(basename "$BAS_FILE")"

# Project root (parent of the scripts/ directory)
PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJECT_ROOT"

NAME="$(basename "$BAS_FILE" .bas)"
BUILD_DIR="build-3ds"
mkdir -p "$BUILD_DIR"

echo "═══════════════════════════════════════════════════════════════"
echo "  Sprout → 3DS"
echo "  Source: $BAS_FILE"
echo "  Output: $BUILD_DIR/$NAME.3dsx"
echo "═══════════════════════════════════════════════════════════════"

# ── Check devkitPro ────────────────────────────────────────────────

if [ -z "$DEVKITPRO" ]; then
    echo "Error: DEVKITPRO env var not set."
    echo "  Install devkitPro: https://devkitpro.org/wiki/Getting_Started"
    exit 1
fi
if [ -z "$DEVKITARM" ]; then
    echo "Error: DEVKITARM env var not set."
    exit 1
fi

# Strip trailing slashes (in case DEVKITPRO is set as /opt/devkitpro/)
DEVKITPRO="${DEVKITPRO%/}"
DEVKITARM="${DEVKITARM%/}"

# Find the libctru directory. On most installs it's $DEVKITPRO/libctru,
# but some setups put it under $DEVKITPRO/portlibs/3ds or elsewhere.
LIBCTRU_DIR=""
for candidate in \
    "$DEVKITPRO/libctru" \
    "$DEVKITPRO/portlibs/3ds" \
    "$DEVKITPRO/portlibs/armv6k"; do
    if [ -f "$candidate/lib/libctru.a" ]; then
        LIBCTRU_DIR="$candidate"
        break
    fi
done

if [ -z "$LIBCTRU_DIR" ]; then
    echo "Error: libctru.a not found in any of:"
    echo "  - $DEVKITPRO/libctru/lib/"
    echo "  - $DEVKITPRO/portlibs/3ds/lib/"
    echo "  - $DEVKITPRO/portlibs/armv6k/lib/"
    echo ""
    echo "Install it: sudo dkp-pacman -S 3ds-libctru"
    echo ""
    echo "Contents of $DEVKITPRO:"
    ls -la "$DEVKITPRO" 2>&1 | head -20
    exit 1
fi

# Find the 3dsx.specs file (needed for linking .3dsx)
SPECS_FILE=""
for candidate in \
    "$LIBCTRU_DIR/lib/3dsx.specs" \
    "$DEVKITPRO/libctru/lib/3dsx.specs" \
    "$DEVKITARM/lib/3dsx.specs" \
    "$DEVKITARM/arm-none-eabi/lib/3dsx.specs" \
    "$DEVKITPRO/devkitARM/lib/3dsx.specs" \
    "$DEVKITPRO/devkitARM/arm-none-eabi/lib/3dsx.specs"; do
    if [ -f "$candidate" ]; then
        SPECS_FILE="$candidate"
        break
    fi
done

if [ -z "$SPECS_FILE" ]; then
    echo "Error: 3dsx.specs not found. Searched:"
    echo "  - $LIBCTRU_DIR/lib/3dsx.specs"
    echo "  - $DEVKITPRO/libctru/lib/3dsx.specs"
    echo "  - $DEVKITARM/lib/3dsx.specs"
    echo "  - $DEVKITARM/arm-none-eabi/lib/3dsx.specs"
    echo "  - $DEVKITPRO/devkitARM/lib/3dsx.specs"
    echo "  - $DEVKITPRO/devkitARM/arm-none-eabi/lib/3dsx.specs"
    echo ""
    echo "Contents of $LIBCTRU_DIR/lib:"
    ls -la "$LIBCTRU_DIR/lib" 2>&1 | head -30
    echo ""
    echo "Try running: find $DEVKITPRO -name '3dsx.specs' 2>/dev/null"
    echo "  ...and let me know what path it's at."
    exit 1
fi

if ! command -v 3dsxtool >/dev/null 2>&1; then
    # 3dsxtool might be in DEVKITPRO/tools/bin
    if [ -x "$DEVKITPRO/tools/bin/3dsxtool" ]; then
        export PATH="$DEVKITPRO/tools/bin:$PATH"
    else
        echo "Error: 3dsxtool not found in PATH."
        echo "  Install it: sudo dkp-pacman -S 3ds-tools"
        exit 1
    fi
fi

echo "✓ devkitPro:  $DEVKITPRO"
echo "✓ libctru:    $LIBCTRU_DIR"
echo "✓ specs:      $SPECS_FILE"
echo "✓ 3dsxtool:   $(command -v 3dsxtool)"
echo ""

# ── Step 1: Build sproutc if needed ────────────────────────────────

if [ ! -x ./sproutc ]; then
    echo "[1/5] Building sproutc compiler..."
    make sproutc
else
    echo "[1/5] sproutc already built."
fi

# ── Step 2: .bas → .c ──────────────────────────────────────────────

echo "[2/5] Compiling $NAME.bas → $NAME.c"
./sproutc "$BAS_FILE" --target=3ds -o "$BUILD_DIR/$NAME.c"

# ── Step 2.5: Build RomFS from assets/ folder ──────────────────────
#
# If there's an assets/ folder next to the .bas file, we:
#   1. Convert each .png to .t3x using tex3ds (citro2d's texture format)
#   2. Copy non-PNG files as-is
#   3. Pass the directory to 3dsxtool with --romfs= in the link step
#
# At runtime, LOADIMAGE("player.png") looks for "romfs:/player.t3x"

BAS_DIR="$(dirname "$BAS_FILE")"
ASSETS_DIR="$BAS_DIR/assets"
ROMFS_DIR="$BUILD_DIR/romfs"
ROMFS_FLAG=""

if [ -d "$ASSETS_DIR" ]; then
    echo "[2.5/5] Building RomFS from $ASSETS_DIR"

    # Check for tex3ds
    if ! command -v tex3ds >/dev/null 2>&1; then
        if [ -x "$DEVKITPRO/tools/bin/tex3ds" ]; then
            export PATH="$DEVKITPRO/tools/bin:$PATH"
        else
            echo "Error: tex3ds not found. Install it: sudo dkp-pacman -S 3ds-tools"
            exit 1
        fi
    fi

    # Clean and create romfs directory
    rm -rf "$ROMFS_DIR"
    mkdir -p "$ROMFS_DIR"

    # Process each file in assets/
    for asset in "$ASSETS_DIR"/*; do
        [ -f "$asset" ] || continue  # skip directories
        fname="$(basename "$asset")"
        ext="${fname##*.}"
        namenoext="${fname%.*}"

        if [ "$(echo "$ext" | tr '[:upper:]' '[:lower:]')" = "png" ]; then
            # Convert PNG → t3x
            echo "  Converting $fname → $namenoext.t3x"
            tex3ds -f rgba -o "$ROMFS_DIR/$namenoext.t3x" "$asset"
        elif [ "$(echo "$ext" | tr '[:upper:]' '[:lower:]')" = "ttf" ]; then
            # Convert TTF → bcfnt (citro2d's font format)
            # mkbcfnt is part of 3ds-tools
            echo "  Converting $fname → $namenoext.bcfnt"
            if command -v mkbcfnt >/dev/null 2>&1; then
                mkbcfnt "$asset" "$ROMFS_DIR/$namenoext.bcfnt"
            elif [ -x "$DEVKITPRO/tools/bin/mkbcfnt" ]; then
                "$DEVKITPRO/tools/bin/mkbcfnt" "$asset" "$ROMFS_DIR/$namenoext.bcfnt"
            else
                echo "    Warning: mkbcfnt not found, copying TTF as-is"
                cp "$asset" "$ROMFS_DIR/"
            fi
        else
            # Copy as-is (text files, data files, .bcfnt, etc.)
            echo "  Copying $fname"
            cp "$asset" "$ROMFS_DIR/"
        fi
    done

    ROMFS_FLAG="--romfs=$ROMFS_DIR"
    echo "  ✓ RomFS ready at $ROMFS_DIR"
else
    echo "[2.5/5] No assets/ folder found — skipping RomFS"
fi

# ── Step 3: .c → .o (plus 3DS runtime) ────────────────────────────

CC="$DEVKITARM/bin/arm-none-eabi-gcc"
CFLAGS="-march=armv6k -mtune=mpcore -mtp=soft -mfloat-abi=hard"
CFLAGS="$CFLAGS -mword-relocations -O2 -Wall"
CFLAGS="$CFLAGS -Iruntime -I$LIBCTRU_DIR/include"

echo "[3/5] Compiling $NAME.c → $NAME.o"
"$CC" $CFLAGS -c "$BUILD_DIR/$NAME.c" -o "$BUILD_DIR/$NAME.o"

echo "[3/5] Compiling nb_runtime.c → nb_runtime.o"
"$CC" $CFLAGS -c runtime/3ds/nb_runtime.c -o "$BUILD_DIR/nb_runtime.o"

# ── Step 4: Link .o → .elf ─────────────────────────────────────────
#
# IMPORTANT: With GCC's linker, libraries must come AFTER the object files
# that reference them. So the order is:
#   <objects> -lctru -lm
# Not:
#   -lctru -lm <objects>     ← would fail with "undefined reference"
#
# citro2d needs citro3d, which needs the GPU. Link order:
#   objects → -lc2d -lc3d -lctru -lm
#
# Note: We link to .elf first, then use 3dsxtool to create the .3dsx
# with the RomFS bundled in.

LDFLAGS="-specs=$SPECS_FILE"
LDFLAGS="$LDFLAGS -march=armv6k -mtune=mpcore -mtp=soft -mfloat-abi=hard"
LDFLAGS="$LDFLAGS -L$LIBCTRU_DIR/lib"
LDLIBS="-lcitro2d -lcitro3d -lctru -lm"

echo "[4/5] Linking → $NAME.elf"
"$CC" $LDFLAGS \
    "$BUILD_DIR/$NAME.o" \
    "$BUILD_DIR/nb_runtime.o" \
    $LDLIBS \
    -o "$BUILD_DIR/$NAME.elf"

# ── Step 5: .elf → .3dsx (with RomFS bundled) ─────────────────────
#
# 3dsxtool converts the .elf into a .3dsx homebrew binary.
# It requires an SMDH file (metadata: title, description, icon).
# The --romfs= flag bundles a directory into the .3dsx as RomFS.

echo "[5/5] Packaging → $NAME.3dsx"

SMDH_FILE="$BUILD_DIR/$NAME.smdh"

# ── Read config.txt if it exists ──
# Place a config.txt next to your .bas file to customize the app metadata:
#   name=My Cool Game
#   description=A game made with Sprout
#   author=Your Name
APP_NAME="$NAME"
APP_DESC="Built with Sprout"
APP_AUTHOR="Sprout"

CONFIG_FILE="$BAS_DIR/config.txt"
if [ -f "$CONFIG_FILE" ]; then
    echo "  Reading config.txt"
    while IFS='=' read -r key value; do
        # Skip comments and empty lines
        case "$key" in
            \#*|"") continue ;;
        esac
        # Trim whitespace
        key=$(echo "$key" | xargs)
        value=$(echo "$value" | xargs)
        case "$key" in
            name)        APP_NAME="$value" ;;
            description) APP_DESC="$value" ;;
            author)      APP_AUTHOR="$value" ;;
        esac
    done < "$CONFIG_FILE"
    echo "  App: $APP_NAME — $APP_DESC — $APP_AUTHOR"
fi

# Generate the SMDH file (app metadata)
if ! command -v smdhtool >/dev/null 2>&1; then
    if [ -x "$DEVKITPRO/tools/bin/smdhtool" ]; then
        export PATH="$DEVKITPRO/tools/bin:$PATH"
    fi
fi

# Find an icon to use
ICON_PATH=""
if [ -f "$BAS_DIR/assets/icon.png" ]; then
    ICON_PATH="$BAS_DIR/assets/icon.png"
elif [ -f "$DEVKITPRO/libctru/default_icon.png" ]; then
    ICON_PATH="$DEVKITPRO/libctru/default_icon.png"
fi

if command -v smdhtool >/dev/null 2>&1; then
    if [ -n "$ICON_PATH" ]; then
        smdhtool --create "$APP_NAME" "$APP_DESC" "$APP_AUTHOR" "$ICON_PATH" "$SMDH_FILE"
    else
        echo "  Warning: no icon found (looked in assets/icon.png and libctru default)"
        smdhtool --create "$APP_NAME" "$APP_DESC" "$APP_AUTHOR" "/dev/null" "$SMDH_FILE" 2>&1 || true
    fi
else
    echo "  Error: smdhtool not found. Install it: sudo dkp-pacman -S 3ds-tools"
    exit 1
fi

# Run 3dsxtool with --smdh and optionally --romfs
if [ -n "$ROMFS_FLAG" ]; then
    3dsxtool "$BUILD_DIR/$NAME.elf" "$BUILD_DIR/$NAME.3dsx" \
        --smdh="$SMDH_FILE" \
        --romfs="$ROMFS_DIR"
    echo "  (with RomFS: $ROMFS_DIR)"
else
    3dsxtool "$BUILD_DIR/$NAME.elf" "$BUILD_DIR/$NAME.3dsx" \
        --smdh="$SMDH_FILE"
fi

# ── Done ───────────────────────────────────────────────────────────

ELF_SIZE=$(stat -c%s "$BUILD_DIR/$NAME.3dsx" 2>/dev/null || stat -f%z "$BUILD_DIR/$NAME.3dsx")
echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  ✓ Build complete!"
echo "  File: $BUILD_DIR/$NAME.3dsx ($ELF_SIZE bytes)"
echo ""
echo "  Run in Citra:    citra $BUILD_DIR/$NAME.3dsx"
echo "  Run on hardware: copy to SD card at"
echo "                   /3ds/$NAME/$NAME.3dsx"
echo "                   Launch via Homebrew Launcher."
echo "═══════════════════════════════════════════════════════════════"
