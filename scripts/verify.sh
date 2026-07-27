#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_DIR=$(mktemp -d /tmp/thermox-verify.XXXXXX)

cleanup() {
    cmake -E remove_directory "$BUILD_DIR"
}
trap cleanup EXIT HUP INT TERM

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" --parallel
ctest --test-dir "$BUILD_DIR" --output-on-failure
"$BUILD_DIR/thermox_cli" solve --model "$ROOT_DIR/core/examples/air_compressor.json" --format json
"$BUILD_DIR/thermox_cli" simulate \
    --model "$ROOT_DIR/core/examples/lumped_thermal_storage.json" \
    --case charge \
    --end-time 10 \
    --format json
