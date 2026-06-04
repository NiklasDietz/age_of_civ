#!/usr/bin/env python3
"""Column-aware CSV comparison for golden regression checks.

Compares two CSV files cell by cell. Integer and string columns must match
exactly; floating-point columns may differ within a tolerance, because some
behaviour-preserving refactors legitimately change the *order* of float
accumulation (re-association), which perturbs the last digits without
changing the result. A cell mismatches when

    abs(a - b) > max(abs_tol, rel_tol * max(|a|, |b|))

Defaults (abs_tol=1e-3, rel_tol=1e-5) sit below the ~6 significant figures the
C++ default ``operator<<`` prints for ``float`` yet well above float32
re-association noise, so genuine logic changes are still caught.

Exit status: 0 when the files are equivalent, 1 on any mismatch (or structural
difference), 2 on usage / IO error. Mismatches are reported with file, line,
column header and both values so a failure is diagnosable without a debugger.
"""

from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path


def _parse_number(text: str):
    """Return (is_int, value) for a numeric cell, or (None, None) otherwise."""
    stripped = text.strip()
    if not stripped:
        return None, None
    try:
        return True, int(stripped)
    except ValueError:
        pass
    try:
        return False, float(stripped)
    except ValueError:
        return None, None


def _cells_match(a: str, b: str, abs_tol: float, rel_tol: float) -> bool:
    if a == b:
        return True
    a_is_int, a_val = _parse_number(a)
    b_is_int, b_val = _parse_number(b)
    if a_val is None or b_val is None:
        return False  # at least one side is non-numeric and strings already differ
    if a_is_int and b_is_int:
        return a_val == b_val  # integer columns are exact
    diff = abs(a_val - b_val)
    return diff <= max(abs_tol, rel_tol * max(abs(a_val), abs(b_val)))


def _read_rows(path: Path) -> list[list[str]]:
    with path.open(newline="") as handle:
        return list(csv.reader(handle))


def compare(baseline: Path, current: Path, abs_tol: float, rel_tol: float,
            max_report: int) -> int:
    base_rows = _read_rows(baseline)
    cur_rows = _read_rows(current)

    if not base_rows and not cur_rows:
        return 0

    base_header = base_rows[0] if base_rows else []
    cur_header = cur_rows[0] if cur_rows else []
    if base_header != cur_header:
        print(f"[csv_diff] header mismatch in {current}", file=sys.stderr)
        print(f"  baseline: {base_header}", file=sys.stderr)
        print(f"  current : {cur_header}", file=sys.stderr)
        return 1

    if len(base_rows) != len(cur_rows):
        print(f"[csv_diff] row-count mismatch in {current}: "
              f"baseline {len(base_rows)} vs current {len(cur_rows)}",
              file=sys.stderr)
        return 1

    mismatches = 0
    for line_no in range(1, len(base_rows)):
        base_row = base_rows[line_no]
        cur_row = cur_rows[line_no]
        if len(base_row) != len(cur_row):
            print(f"[csv_diff] {current}:{line_no + 1} column-count "
                  f"mismatch ({len(base_row)} vs {len(cur_row)})",
                  file=sys.stderr)
            mismatches += 1
            if mismatches >= max_report:
                break
            continue
        for col, (a, b) in enumerate(zip(base_row, cur_row)):
            if not _cells_match(a, b, abs_tol, rel_tol):
                header = base_header[col] if col < len(base_header) else f"col{col}"
                print(f"[csv_diff] {current}:{line_no + 1} column '{header}': "
                      f"baseline='{a}' current='{b}'", file=sys.stderr)
                mismatches += 1
                if mismatches >= max_report:
                    print(f"[csv_diff] ... stopping after {max_report} mismatches",
                          file=sys.stderr)
                    return 1
    return 1 if mismatches else 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Column-aware CSV diff for golden regression.")
    parser.add_argument("baseline", type=Path)
    parser.add_argument("current", type=Path)
    parser.add_argument("--abs-tol", type=float, default=1e-3,
                        help="absolute tolerance for float columns (default 1e-3)")
    parser.add_argument("--rel-tol", type=float, default=1e-5,
                        help="relative tolerance for float columns (default 1e-5)")
    parser.add_argument("--max-report", type=int, default=20,
                        help="stop after this many reported mismatches (default 20)")
    args = parser.parse_args()

    for path in (args.baseline, args.current):
        if not path.is_file():
            print(f"[csv_diff] missing file: {path}", file=sys.stderr)
            return 2

    return compare(args.baseline, args.current, args.abs_tol, args.rel_tol,
                   args.max_report)


if __name__ == "__main__":
    sys.exit(main())
