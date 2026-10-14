#!/usr/bin/env python3
"""
Non-destructive Git Checkpoint & Blast-Radius Guard for Agentic / Vibe Coding.
- `status`: reports modified/untracked files, touched engine domains, and .clang-tidy safety.
- `snapshot --label <name>`: creates a non-destructive git snapshot under refs/checkpoints/<name> without touching the working tree.
- `list`: lists saved checkpoints.
"""
from __future__ import annotations

import argparse
import datetime
import subprocess
import sys
from pathlib import Path


def run_git(args: list[str]) -> tuple[int, str]:
    proc = subprocess.run(["git", "-c", "core.safecrlf=false", *args], capture_output=True, text=True, encoding="utf-8", errors="replace")
    return proc.returncode, (proc.stdout + proc.stderr).strip()


def check_clang_tidy_safety() -> list[str]:
    ct = Path(".clang-tidy")
    issues: list[str] = []
    if not ct.exists():
        return issues
    in_checks = False
    for lineno, line in enumerate(ct.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
        if line.startswith("Checks:"):
            in_checks = True
            continue
        if in_checks:
            if line and not line[0].isspace():
                in_checks = False
            elif "#" in line:
                issues.append(f".clang-tidy:{lineno} has '#' inside Checks: scalar (kills rule globs!)")
    return issues


def cmd_status() -> int:
    code, out = run_git(["status", "--porcelain"])
    if code != 0:
        print(f"ERROR: git status failed: {out}")
        return 1

    lines = [l for l in out.splitlines() if l.strip()]
    domains: set[str] = set()
    for l in lines:
        path_str = l[3:].strip().replace("\\", "/")
        parts = path_str.split("/")
        if len(parts) >= 2 and parts[0] == "engine":
            domains.add(f"engine/{parts[1]}")
        elif parts:
            domains.add(parts[0])

    _, diff_stat = run_git(["diff", "--shortstat"])
    tidy_issues = check_clang_tidy_safety()

    print(f"GUARD_STATUS: changed_files={len(lines)} touched_scopes=[{', '.join(sorted(domains)) or 'clean'}]")
    if diff_stat:
        print(f"DIFF_STAT: {diff_stat}")
    for iss in tidy_issues:
        print(f"CRITICAL_WARNING: {iss}")

    if tidy_issues:
        print("VERDICT: BLOCK (.clang-tidy scalar corruption)")
        return 1
    print("VERDICT: SAFE")
    return 0


def cmd_snapshot(label: str) -> int:
    ts = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    safe_label = "".join(c if c.isalnum() or c in "-_" else "-" for c in label)
    ref_name = f"refs/checkpoints/{ts}-{safe_label}"

    code, sha = run_git(["stash", "create", f"argon-checkpoint: {label}"])
    if code != 0 or not sha:
        # Working tree matches HEAD; use HEAD commit SHA
        _, sha = run_git(["rev-parse", "HEAD"])

    code, out = run_git(["update-ref", ref_name, sha])
    if code != 0:
        print(f"ERROR: Failed to save ref {ref_name}: {out}")
        return 1

    print(f"CHECKPOINT_SAVED: ref={ref_name} sha={sha[:12]} (working tree untouched)")
    return 0


def cmd_list() -> int:
    _, out = run_git(["for-each-ref", "--sort=-creatordate", "--format=%(refname:short) | %(objectname:short) | %(subject)", "refs/checkpoints/"])
    if not out:
        print("CHECKPOINTS: (none)")
    else:
        print("CHECKPOINTS:")
        for line in out.splitlines()[:15]:
            print(f"  {line}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Git Checkpoint & Blast-Radius Guard.")
    sub = parser.add_subparsers(dest="cmd", required=True)
    sub.add_parser("status", help="Audit working tree blast radius and .clang-tidy safety")
    snap = sub.add_parser("snapshot", help="Create non-destructive checkpoint ref")
    snap.add_argument("--label", default="auto", help="Checkpoint label")
    sub.add_parser("list", help="List recent checkpoints")
    args = parser.parse_args()

    if args.cmd == "status":
        return cmd_status()
    if args.cmd == "snapshot":
        return cmd_snapshot(args.label)
    if args.cmd == "list":
        return cmd_list()
    return 0


if __name__ == "__main__":
    sys.exit(main())
