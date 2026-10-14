#!/usr/bin/env python3
"""Fast #include layer DAG verifier for CHROMODYNAMIC Engine (supports nested L0..L6 subsystems)."""
import argparse
import re
import sys
from pathlib import Path

LAYER_RANK = {
    "foundation": 0,
    "asset": 2,
    "imgdiff": 2,
    "texture_synth": 2,
    "render": 3,
    "world": 3,
    "ui": 3,
    "imgui_backend": 3,
    "runtime": 4,
    "game": 4,
    "editor": 6,
    "profiler_overlay": 5,
}


def classify_source_path(rel_parts: tuple[str, ...]) -> tuple[str, int]:
    rel_posix = "/".join(rel_parts)
    if rel_posix.startswith("ui/editor/"):
        return "editor", 6
    if rel_posix.startswith("ui/input/") or rel_posix.endswith("AndroidInputBridge.hpp"):
        return "input", 2
    if rel_posix.startswith("foundation/profile_"):
        return "profiler_overlay", 5
    top = rel_parts[0]
    return top, LAYER_RANK.get(top, 2)


def classify_include(inc: str) -> tuple[str, int] | None:
    norm = "/" + inc.strip("/")
    if "/editor/" in norm or "/ui/editor/" in norm:
        return "editor", 6
    if "/ui/input/" in norm:
        return "input", 2
    if "/platform/" in norm:
        return "foundation", 0
    for dst_top, dst_rank in LAYER_RANK.items():
        if f"/{dst_top}/" in norm:
            return dst_top, dst_rank
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify C++23 engine layer DAG.")
    parser.add_argument("--engine-root", "--root", dest="engine_root", default="engine", help="Path to engine/ directory")
    args = parser.parse_args()

    root = Path(args.engine_root)
    if not root.is_dir():
        print(f"SKIP: {root} not found")
        return 0

    inc_re = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')
    violations: list[str] = []
    files_scanned = 0

    for fp in list(root.rglob("*.hpp")) + list(root.rglob("*.h")) + list(root.rglob("*.cpp")):
        rel = fp.relative_to(root)
        if not rel.parts:
            continue
        src_name, src_rank = classify_source_path(rel.parts)
        files_scanned += 1
        txt = fp.read_text(encoding="utf-8", errors="replace")
        for lineno, line in enumerate(txt.splitlines(), 1):
            m = inc_re.match(line)
            if not m:
                continue
            inc = m.group(1).replace("\\", "/")
            dst = classify_include(inc)
            if dst is not None:
                dst_name, dst_rank = dst
                if dst_rank > src_rank:
                    violations.append(
                        f"{fp}:{lineno} [{src_name} (L{src_rank}) -> {dst_name} (L{dst_rank})]: #include \"{inc}\""
                    )

    print(f"DAG_SCAN: files={files_scanned} violations={len(violations)}")
    for v in violations[:30]:
        print(f"VIOLATION: {v}")

    if violations:
        print("VERDICT: VETO (Layer violation detected)")
        return 1
    print("VERDICT: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
