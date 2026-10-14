#!/usr/bin/env python3
"""Deterministic HUMAN_VOICE.md compliance checker (forbidden words, burstiness/variance, conjunctions, em-dash ban)."""
import re
import statistics
import sys
from pathlib import Path

FORBIDDEN = [
    "delve", "tapestry", "realm", "ever-evolving", "ever-growing", "ever-changing",
    "embark", "harness", "harnessing", "leverage", "leveraging", "pivotal",
    "underscore", "underscoring", "moreover", "furthermore", "additionally",
    "in conclusion", "to summarize", "in summary", "it is important to note",
    "it should be noted", "it is worth noting", "it's worth mentioning",
    "seamless", "seamlessly", "groundbreaking", "revolutionize", "unleashing",
    "foster", "elevate", "testament to", "holistic", "multifaceted",
    "paradigm shift", "synergize", "synergy",
    "yolculuğa çıkmak", "sürekli gelişen", "köprü kurmak", "özetle",
    "bunun yanı sıra", "şunu belirtmek gerekir ki", "şunu vurgulamak önemlidir ki",
    "göz önünde bulundurulduğunda",
]


def check_file(path: Path) -> int:
    raw = path.read_text(encoding="utf-8", errors="replace")
    lines = raw.splitlines()
    issues: list[str] = []

    for idx, line in enumerate(lines, 1):
        if line.lstrip().startswith("%"):
            continue
        low = line.lower()
        for word in FORBIDDEN:
            if re.search(r"\b" + re.escape(word) + r"\b", low):
                issues.append(f"{path}:{idx}: forbidden phrase '{word}'")
        if "\u2014" in line or "---" in line:
            issues.append(f"{path}:{idx}: forbidden em-dash ('\u2014' or '---')")

    clean_text = re.sub(r"%.*", "", raw)
    conj_hits = len(re.findall(r"(?m)^(?:However|Moreover|Furthermore|Additionally|Therefore|Indeed),", clean_text))
    if conj_hits > 2:
        issues.append(f"{path}: sentence-initial conjunction count={conj_hits} (max 2 allowed)")

    sents = [s.strip() for s in re.split(r"(?<=[.!?])\s+", clean_text) if len(s.strip().split()) > 3]
    if len(sents) >= 4:
        lens = [len(s.split()) for s in sents]
        mn, mx = min(lens), max(lens)
        sd = statistics.pstdev(lens)
        ratio = mx / max(mn, 1)
        print(f"STATS [{path.name}]: n={len(lens)} min={mn} max={mx} ratio={ratio:.2f} stdev={sd:.1f}")
        if ratio < 3.0 or sd < 4.0:
            issues.append(f"{path}: low burstiness (max/min={ratio:.2f} < 3.0 or stdev={sd:.1f} < 4.0)")

    for item in issues[:30]:
        print(f"VIOLATION: {item}")

    if issues:
        print(f"VERDICT [{path.name}]: FAIL ({len(issues)} violations)")
        return 1
    print(f"VERDICT [{path.name}]: PASS")
    return 0


def main() -> int:
    if len(sys.argv) < 2:
        print("Usage: check_human_voice.py <file_or_dir>")
        return 2
    target = Path(sys.argv[1])
    files = [target] if target.is_file() else (list(target.rglob("*.tex")) + list(target.rglob("*.md")))
    rc = 0
    for f in files:
        if check_file(f) != 0:
            rc = 1
    return rc


if __name__ == "__main__":
    sys.exit(main())
