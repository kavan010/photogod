#!/usr/bin/env bash
# dev.sh — "npm run dev" for PhotoGod.
#
# Watches src/ and resources/, rebuilds incrementally, and restarts the app
# on every save. Any file you pass through is reopened after each restart,
# so you land back where you were instead of on the home screen.
#
#   ./dev.sh                    # plain dev loop
#   ./dev.sh saves/test.pgd     # reopen this project on every restart
#   ./dev.sh --build-only       # rebuild on change, don't launch (fast syntax check)
#
# Uses a separate build-dev/ dir (Debug + ccache) so the Release build/ that
# ships is never clobbered.

set -u
cd "$(dirname "$0")"

BUILD_DIR=build-dev
BUILD_ONLY=0
ARGS=()
for a in "$@"; do
  case "$a" in
    --build-only) BUILD_ONLY=1 ;;
    *) ARGS+=("$a") ;;
  esac
done

if ! command -v watchexec >/dev/null 2>&1; then
  echo "dev.sh: watchexec not found. Install it with:" >&2
  echo "    sudo pacman -S watchexec ccache" >&2
  exit 1
fi

# Configure once. Debug build + ccache keeps the edit->run cycle short.
if [ ! -f "$BUILD_DIR/build.ninja" ]; then
  echo ">> configuring $BUILD_DIR (Debug, ccache)"
  CCACHE_FLAG=()
  if command -v ccache >/dev/null 2>&1; then
    CCACHE_FLAG=(-DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
  else
    echo ">> ccache not found; builds will be slower (sudo pacman -S ccache)"
  fi
  cmake -S . -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    "${CCACHE_FLAG[@]}" || exit 1
fi

# The command watchexec re-runs on every change. It restarts the app itself
# (watchexec -r kills the previous process group first).
if [ "$BUILD_ONLY" = 1 ]; then
  RUN_CMD="ninja -C $BUILD_DIR"
else
  printf -v QUOTED_ARGS '%q ' "${ARGS[@]+"${ARGS[@]}"}"
  RUN_CMD="ninja -C $BUILD_DIR && exec ./$BUILD_DIR/photogod ${QUOTED_ARGS}"
fi

echo ">> watching src/ resources/ CMakeLists.txt  (Ctrl-C to stop)"
exec watchexec \
  --restart \
  --clear \
  --watch src --watch resources --watch CMakeLists.txt \
  --exts cpp,h,hpp,qrc,svg,txt \
  --debounce 200ms \
  --shell=bash \
  -- "$RUN_CMD"
