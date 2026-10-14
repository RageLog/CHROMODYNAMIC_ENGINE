#!/usr/bin/env python3
"""
Deterministic Failure Log Parser for C++23 Compiler (MSVC/Clang/GCC), Linker (LLD/LINK),
GTest assertions, and Sanitizers (ASAN/UBSAN/TSan).
Extracts only the actionable root error lines to prevent context bloat.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


PATTERNS = [
    ("COMPILER_ERROR", re.compile(r"^(.+?)\((\d+)(?:,\d+)?\)\s*:\s*(?:fatal )?error\s+([A-Z0-9]+):\s*(.+)$")),
    ("CLANG_GCC_ERROR", re.compile(r"^(.+?):(\d+):(?:\d+:)?\s*(?:fatal )?error:\s*(.+)$")),
    ("SANITIZER", re.compile(r"(ERROR:\s*(?:AddressSanitizer|UndefinedBehaviorSanitizer|ThreadSanitizer):\s*.+)")),
    ("GTEST_FAILURE", re.compile(r"^(.+?)\((\d+)\):\s*error:\s*(.+)$")),
    ("GTEST_FAILED_CASE", re.compile(r"^\[\s*FAILED\s*\]\s*(.+)$")),
    ("LINKER_ERROR", re.compile(r"(?:lld-link|LINK|ld)\s*:\s*error\s*:\s*(.+)")),
]


def parse_lines(lines: list[str], max_hits: int = 12) -> int:
    hits: list[str] = []
    seen: set[str] = set()

    for line in lines:
        s = line.strip()
        if not s:
            continue
        for kind, pat in PATTERNS:
            m = pat.search(s)
            if m:
                entry = f"[{kind}] {s}"
                if entry not in seen:
                    seen.add(entry)
                    hits.append(entry)
                break
        if len(hits) >= max_hits:
            break

    print(f"RCA_EXTRACTION: total_lines={len(lines)} actionable_errors={len(hits)}")
    for h in hits:
        print(f"  {h}")
    if not hits:
        print("  (No structured compiler/test/sanitizer error pattern matched; showing last 10 lines)")
        for tail in lines[-10:]:
            print(f"  {tail.rstrip()}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Extract actionable root errors from build/test logs.")
    parser.add_argument("--file", default=None, help="Path to log file (reads stdin if omitted)")
    parser.add_argument("--max", type=int, default=12, help="Max errors to display")
    args = parser.parse_args()

    if args.file:
        p = Path(args.file)
        if not p.exists():
            print(f"ERROR: Log file not found: {p}")
            return 1
        lines = p.read_text(encoding="utf-8", errors="replace").splitlines()
    else:
        lines = sys.stdin.read().splitlines()

    return parse_lines(lines, args.max)


if __name__ == "__main__":
    sys.exit(main())
