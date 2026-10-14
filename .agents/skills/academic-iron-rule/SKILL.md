---
name: academic-iron-rule
description: >-
  Enforces the Q1 Academic Iron Rule (local PDF verification, sha256 MANIFEST.csv,
  bibliography.bib completeness) and HUMAN_VOICE.md anti-AI-cliche post-write checks.
  Use whenever searching academic literature, verifying citations, auditing LaTeX/markdown
  manuscripts, or writing academic sections and research notes.
---

# Academic Iron Rule & Human Voice Verification

## 1. The Iron Rule (Zero Exceptions)

An academic reference may be cited or added to `research/library/bibliography.bib` **if and only if**:
1. `research/library/pdf/<bibkey>.pdf` exists on disk and is a valid multi-page PDF.
2. `research/library/MANIFEST.csv` has a verified row with matching `sha256`.
3. `research/library/bibliography.bib` contains a complete entry (`author`, `title`, `year`, `journal`/`booktitle`, `doi` or `eprint`).
4. `research/library/notes/<bibkey>.md` exists.
- Unretrievable papers (paywall, 404, mismatch) **must** be logged in `research/library/ATTEMPTS.md` and **never** cited.

## 2. Automated Verification Scripts (Token & Latency Savers)

Instead of writing ad-hoc shell loops, run these deterministic Python scripts:

### A. Verify `MANIFEST.csv` ↔ `bibliography.bib` ↔ Local PDFs (`sha256`)
```powershell
python "C:\Users\cemal\.gemini\config\plugins\argon-core\skills\academic-iron-rule\scripts\verify_manifest.py" --root "research/library" --tex-dir "research/papers"
```

### B. Verify `HUMAN_VOICE.md` Compliance (Forbidden Words, Burstiness, Conjunctions, Em-Dash Ban)
```powershell
python "C:\Users\cemal\.gemini\config\plugins\argon-core\skills\academic-iron-rule\scripts\check_human_voice.py" "path/to/file.tex"
```
