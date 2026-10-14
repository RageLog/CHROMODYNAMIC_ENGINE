#!/usr/bin/env python3
r"""
Deterministic Iron Rule (Demir Kural) verifier for CHROMODYNAMIC_ENGINE.
Checks:
1. Every entry in research/library/MANIFEST.csv (supporting comment lines '#' and both bibkey + bibtex_anchor) has a real, non-empty PDF on disk (unless status/demir_kural_status is STAGED/PENDING_PDF, which is flagged as a warning unless cited).
2. The SHA-256 of each verified PDF matches the sha256 recorded in MANIFEST.csv.
3. Every BibTeX key in research/library/bibliography.bib is backed by a MANIFEST.csv entry.
4. Optionally checks all \cite{...} keys in specified .tex files against both MANIFEST.csv and bibliography.bib.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import pathlib
import re
import sys


HEX64_RE = re.compile(r"^[0-9a-fA-F]{64}$")


def sha256_file(path: pathlib.Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def parse_bib_keys(bib_path: pathlib.Path) -> set[str]:
    if not bib_path.exists():
        return set()
    content = bib_path.read_text(encoding="utf-8", errors="replace")
    return set(re.findall(r"@\w+\s*\{\s*([^,\s]+)\s*,", content))


def parse_tex_citations(tex_paths: list[pathlib.Path]) -> dict[str, list[str]]:
    cite_re = re.compile(r"\\(?:cite|parencite|textcite|citep|citet|nocite)\*?(?:\[[^\]]*\])*\{([^}]+)\}")
    found: dict[str, list[str]] = {}
    for path in tex_paths:
        if not path.exists():
            continue
        for idx, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), start=1):
            clean = line.split("%", 1)[0]
            for match in cite_re.finditer(clean):
                for key in match.group(1).split(","):
                    k = key.strip()
                    if k:
                        found.setdefault(k, []).append(f"{path.name}:{idx}")
    return found


def extract_row_fields(raw_row: list[str], header: list[str]) -> dict[str, str]:
    row_dict = {header[i]: (raw_row[i].strip() if i < len(raw_row) else "") for i in range(len(header))}
    # Handle rows where an extra DOI column shifted pdf_relpath and sha256 by 1 position
    if not HEX64_RE.match(row_dict.get("sha256", "")):
        for cell in raw_row:
            c = cell.strip()
            if HEX64_RE.match(c):
                row_dict["sha256"] = c
            elif c.endswith(".pdf"):
                row_dict["pdf_relpath"] = c
        if len(raw_row) >= 2:
            row_dict["bibtex_anchor"] = raw_row[-2].strip()
            row_dict["demir_kural_status"] = raw_row[-1].strip()
    return row_dict


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify Academic Iron Rule (MANIFEST.csv + PDF sha256 + BibTeX)")
    parser.add_argument("--library-dir", default="research/library", help="Path to research/library directory")
    parser.add_argument("tex_files", nargs="*", help="Optional .tex files to audit for \\cite{} keys")
    args = parser.parse_args()

    lib_dir = pathlib.Path(args.library_dir)
    manifest_path = lib_dir / "MANIFEST.csv"
    bib_path = lib_dir / "bibliography.bib"

    errors: list[str] = []
    warnings: list[str] = []

    if not manifest_path.exists():
        print(f"ERROR: Missing {manifest_path}")
        return 1

    manifest_keys: set[str] = set()
    staged_keys: set[str] = set()
    verified_count = 0

    with manifest_path.open("r", encoding="utf-8", errors="replace", newline="") as f:
        non_comment_lines = [line for line in f if line.strip() and not line.lstrip().startswith("#")]

    reader = csv.reader(non_comment_lines)
    try:
        header = [h.strip() for h in next(reader)]
    except StopIteration:
        print("ERROR: Empty MANIFEST.csv")
        return 1

    for raw_row in reader:
        if not raw_row:
            continue
        row = extract_row_fields(raw_row, header)
        bibkey = row.get("bibkey") or row.get("bibtex_key") or row.get("id") or ""
        anchor = row.get("bibtex_anchor") or ""
        status = (row.get("demir_kural_status") or row.get("status") or "").upper()

        if not bibkey:
            continue

        keys_for_row = {k for k in (bibkey, anchor) if k}
        if status in {"STAGED", "PENDING_PDF"}:
            staged_keys.update(keys_for_row)
            warnings.append(f"[W-STAGED] '{bibkey}' ({anchor}) is marked {status} — cannot be cited until PDF is verified")
            continue

        manifest_keys.update(keys_for_row)

        rel_pdf = row.get("pdf_relpath") or row.get("pdf_path") or row.get("file") or f"pdf/{bibkey}.pdf"
        # Resolve relative to either repo root or research/library
        pdf_path = pathlib.Path(rel_pdf)
        if not pdf_path.exists():
            pdf_path = lib_dir / rel_pdf
        if not pdf_path.exists():
            pdf_path = lib_dir / "pdf" / pathlib.Path(rel_pdf).name

        if not pdf_path.exists() or pdf_path.stat().st_size == 0:
            errors.append(f"[B-PDF] Missing or empty local PDF for '{bibkey}': expected {rel_pdf}")
            continue

        expected_sha = (row.get("sha256") or "").lower()
        if expected_sha:
            actual_sha = sha256_file(pdf_path).lower()
            if actual_sha != expected_sha:
                errors.append(f"[B-SHA256] sha256 mismatch for '{bibkey}': expected={expected_sha[:12]} actual={actual_sha[:12]}")
            else:
                verified_count += 1
        else:
            warnings.append(f"[W-SHA256] No sha256 column value for '{bibkey}' in MANIFEST.csv")

    bib_keys = parse_bib_keys(bib_path)
    for bkey in sorted(bib_keys):
        if bkey in staged_keys:
            warnings.append(f"[W-BIB-STAGED] BibTeX key '{bkey}' is in bibliography.bib but its PDF is still STAGED in MANIFEST.csv")
        elif bkey not in manifest_keys:
            errors.append(f"[B-MANIFEST] BibTeX key '{bkey}' is in bibliography.bib but missing from MANIFEST.csv")

    tex_paths = [pathlib.Path(p) for p in args.tex_files]
    cited = parse_tex_citations(tex_paths)
    for ckey, locs in sorted(cited.items()):
        loc_str = ", ".join(locs[:3])
        if ckey in staged_keys:
            errors.append(f"[B-CITE-STAGED] Cited key '{ckey}' ({loc_str}) is STAGED/PENDING_PDF — Iron Rule blocks citation")
        elif ckey not in manifest_keys:
            errors.append(f"[B-CITE-MANIFEST] Cited key '{ckey}' ({loc_str}) is NOT in MANIFEST.csv")
        if ckey not in bib_keys:
            errors.append(f"[B-CITE-BIB] Cited key '{ckey}' ({loc_str}) is NOT in bibliography.bib")

    print(
        f"SUMMARY: verified_pdfs={verified_count} staged={len(staged_keys)//2 or len(staged_keys)} "
        f"bib_entries={len(bib_keys)} errors={len(errors)} warnings={len(warnings)}"
    )
    for e in errors:
        print(f"ERROR: {e}")
    for w in warnings:
        print(f"WARN:  {w}")

    if errors:
        print("VERDICT: BLOCK")
        return 1
    print("VERDICT: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
