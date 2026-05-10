#!/usr/bin/env bash
# Compare two snapshot directories produced by dump.sh.
# Usage: diff.sh /tmp/aoc_dbg/A /tmp/aoc_dbg/B
set -euo pipefail
if [ "$#" -lt 2 ]; then
    echo "usage: $0 <dirA> <dirB>" >&2
    exit 2
fi
A="$1"; B="$2"
echo "=== info.json ==="
diff -u "${A}/info.json" "${B}/info.json" || true
echo "=== plates.csv ==="
diff -u "${A}/plates.csv" "${B}/plates.csv" || true
echo "=== grid.txt cell counts (terrain glyphs) ==="
for d in "${A}" "${B}"; do
    if [ -f "${d}/grid.txt" ]; then
        echo "-- ${d} --"
        tail -n +2 "${d}/grid.txt" | grep -oE '[gTfjmDPO\^\-MIr:.,]' | sort | uniq -c | sort -rn
    fi
done
