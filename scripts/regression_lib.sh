#!/usr/bin/env bash
# Shared helpers for the golden-regression scripts. Sourced, not executed.
#
# Locates built tool binaries across the project's several build trees and
# exposes REPO_ROOT. Determinism note: every regression run must pin
# OMP_NUM_THREADS=1 so OpenMP-parallel map generation is reproducible.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export OMP_NUM_THREADS=1

# find_tool <binary-name> <ENV_OVERRIDE_VAR>
# Echoes an absolute path to the binary, or returns 1 if not found.
find_tool() {
    local name="$1"
    local override_var="$2"
    local override="${!override_var:-}"
    if [[ -n "${override}" ]]; then
        if [[ -x "${override}" ]]; then
            printf '%s\n' "${override}"
            return 0
        fi
        echo "regression: ${override_var}='${override}' is not executable" >&2
        return 1
    fi
    local dir
    for dir in build/release build/debug build/perf build_rwdi build_dbg build; do
        if [[ -x "${REPO_ROOT}/${dir}/${name}" ]]; then
            printf '%s\n' "${REPO_ROOT}/${dir}/${name}"
            return 0
        fi
    done
    local found
    found="$(find "${REPO_ROOT}" -type f -name "${name}" -perm -u+x 2>/dev/null \
        | grep -v '/.claude/' | head -1 || true)"
    if [[ -n "${found}" ]]; then
        printf '%s\n' "${found}"
        return 0
    fi
    echo "regression: could not find '${name}'. Build first, or set ${override_var}." >&2
    return 1
}
