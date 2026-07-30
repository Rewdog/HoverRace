#!/bin/zsh
#
# Configure and build HoverRace on macOS (Apple Silicon).
#
# Why this script exists:
#
#   Homebrew's "sdl2" formula is now sdl2-compat, which implements the SDL2 API
#   on top of SDL3.  HoverRace does not render correctly against it, so the build
#   uses a real SDL2 staged under .deps/sdl2 instead.  That directory has to come
#   first on PKG_CONFIG_PATH, otherwise pkg-config resolves sdl2 to the compat
#   shim in /opt/homebrew and the game builds against the wrong library.
#
# Usage:
#   bin/build-macos.sh          # configure (if needed) and build
#   bin/build-macos.sh clean    # wipe the build directory first
#
set -e

SRC_DIR=${0:A:h:h}
BUILD_DIR="$SRC_DIR/build"
SDL_PREFIX="$SRC_DIR/.deps/sdl2"

if [[ "$1" == "clean" ]]; then
	echo "==> Removing $BUILD_DIR"
	rm -rf "$BUILD_DIR"
fi

if [[ ! -f "$SDL_PREFIX/lib/pkgconfig/sdl2.pc" ]]; then
	echo "error: real SDL2 not found at $SDL_PREFIX" >&2
	echo "       See README-macos.md for how to build it." >&2
	exit 1
fi

# Real SDL2 first, then Homebrew for everything else.
# openal-soft and curl are keg-only, so their .pc files are not in the
# top-level Homebrew pkgconfig directory and have to be added explicitly.
PC_PATH="$SDL_PREFIX/lib/pkgconfig"
for formula in openal-soft freealut libxdg-basedir curl; do
	prefix=$(brew --prefix "$formula" 2>/dev/null) || continue
	[[ -d "$prefix/lib/pkgconfig" ]] && PC_PATH="$PC_PATH:$prefix/lib/pkgconfig"
done
export PKG_CONFIG_PATH="$PC_PATH:/opt/homebrew/lib/pkgconfig:/opt/homebrew/share/pkgconfig"

if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
	echo "==> Configuring"
	cmake -S "$SRC_DIR" -B "$BUILD_DIR" \
		-DCMAKE_BUILD_TYPE=RelWithDebInfo \
		-DCMAKE_PREFIX_PATH="/opt/homebrew"
fi

echo "==> Building"
cmake --build "$BUILD_DIR" -j"$(sysctl -n hw.ncpu)"

echo
echo "==> Done: $BUILD_DIR/bin/hoverrace"
