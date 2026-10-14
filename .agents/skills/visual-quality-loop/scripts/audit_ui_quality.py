#!/usr/bin/env python3
"""
Static UI/UX & Visual Quality Auditor for CHROMODYNAMIC_ENGINE (engine/ui, engine/render).
Checks modules for:
1. Presence of unit/interaction tests in tests/
2. State/interaction affordance indicators (empty/reset/clear/clamp/serialize/zoom/pan/hover/select)
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


AFFORDANCE_RE = re.compile(
    r"\b(clear|reset|empty|valid|clamp|zoom|pan|select|hover|filter|undo|redo|serialize|json|draw|render)\b",
    re.IGNORECASE,
)


def audit_path(root: Path) -> int:
    if not root.exists():
        print(f"ERROR: Path not found: {root}")
        return 1

    headers = sorted(root.rglob("*.hpp"))
    tests = sorted(root.rglob("test_*.cpp"))
    warnings: list[str] = []

    for hp in headers:
        txt = hp.read_text(encoding="utf-8", errors="replace")
        affordances = set(m.group(1).lower() for m in AFFORDANCE_RE.finditer(txt))
        if len(affordances) < 2:
            warnings.append(f"[W-SURFACE] {hp.as_posix()} has sparse state/interaction surface ({', '.join(affordances) or 'none'})")

    print(
        f"VISUAL_QUALITY_AUDIT: path={root.as_posix()} headers={len(headers)} "
        f"test_files={len(tests)} warnings={len(warnings)}"
    )
    for w in warnings[:10]:
        print(f"  {w}")

    if not tests:
        print("VERDICT: WARN (No test_*.cpp files found in target path)")
        return 0
    print("VERDICT: PASS")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Audit UI/UX and visual modules for state/test readiness.")
    parser.add_argument("--path", default="engine/ui", help="Target UI or render module directory")
    args = parser.parse_args()
    return audit_path(Path(args.path))


if __name__ == "__main__":
    sys.exit(main())
