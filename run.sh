#!/bin/bash
# Launch Age of Civilization from the build directory
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
export LSAN_OPTIONS="suppressions=${SCRIPT_DIR}/asan_suppressions.txt"
cd "${SCRIPT_DIR}/build/debug" && exec ./age_of_civ "$@"
