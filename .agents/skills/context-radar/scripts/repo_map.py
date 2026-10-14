#!/usr/bin/env python3
"""
Fast Context Radar for CHROMODYNAMIC_ENGINE.
Extracts CMake targets, public headers, and C++23 symbol definitions (class/struct/enum/concept)
with exact line numbers in a compact, token-efficient table.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


SYM_RE = re.compile(
    r"^\s*(?:template\s*<[^>]*>\s*)?(?:class|struct|enum\s+class|concept)\s+(?:[A-Z0-9_]+_API\s+)?([A-Za-z_][A-Za-z0-9_]*)"
)
CMAKE_TARGET_RE = re.compile(r"add_library\s*\(\s*([A-Za-z0-9_:-]+)")


def scan_directory(root: Path, query: str | None, max_items: int) -> int:
    if not root.exists():
        print(f"ERROR: Path not found: {root}")
        return 1

    q_lower = query.lower() if query else None
    cmake_targets: list[str] = []
    for cm in root.rglob("CMakeLists.txt"):
        txt = cm.read_text(encoding="utf-8", errors="replace")
        for m in CMAKE_TARGET_RE.finditer(txt):
            tname = m.group(1)
            if not tname.startswith("$"):
                cmake_targets.append(f"{tname} ({cm.parent.as_posix()})")

    headers = sorted(list(root.rglob("*.hpp")) + list(root.rglob("*.h")))
    rows: list[str] = []

    for hp in headers:
        rel = hp.as_posix()
        txt = hp.read_text(encoding="utf-8", errors="replace")
        syms: list[str] = []
        for lineno, line in enumerate(txt.splitlines(), 1):
            m = SYM_RE.match(line)
            if m:
                sname = m.group(1)
                if not sname.endswith(";"):
                    syms.append(f"{sname}:L{lineno}")
        if q_lower:
            matched_syms = [s for s in syms if q_lower in s.lower()]
            if q_lower not in rel.lower() and not matched_syms:
                continue
            display_syms = matched_syms if matched_syms else syms[:6]
        else:
            display_syms = syms[:6]

        sym_str = ", ".join(display_syms) if display_syms else "(functions/constants)"
        rows.append(f"  {rel} -> [{sym_str}]")
        if len(rows) >= max_items:
            break

    print(f"RADAR_SUMMARY: root={root.as_posix()} headers_scanned={len(headers)} cmake_targets={len(cmake_targets)} matches={len(rows)}")
    if cmake_targets and not q_lower:
        print("CMAKE_TARGETS: " + ", ".join(cmake_targets[:15]))
    for r in rows:
        print(r)
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate compact symbol & header radar map.")
    parser.add_argument("--path", default="engine", help="Directory to scan (e.g. engine/render)")
    parser.add_argument("--query", default=None, help="Optional case-insensitive symbol or filename filter")
    parser.add_argument("--max", type=int, default=35, help="Max header rows to output")
    args = parser.parse_args()
    return scan_directory(Path(args.path), args.query, args.max)


if __name__ == "__main__":
    sys.exit(main())
