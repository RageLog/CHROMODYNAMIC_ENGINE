# research/library/ -- Demir Kural verified citation library

This directory enforces the project's **Demir Kural** (Iron Rule): no academic
claim may appear in code comments, ADRs, or written deliverables without a
verified PDF on disk, a SHA-256 hash, a notes file summarising the claim, and a
BibTeX entry. The structure:

```
research/library/
  MANIFEST.csv          -- source-of-truth registry (one row per paper)
  bibliography.bib      -- BibTeX entries matching MANIFEST.csv bibkeys
  pdf/                  -- on-disk PDFs (gitignored if filenames + hashes are
                            in MANIFEST.csv; commit policy TBD)
  notes/<bibkey>.md     -- per-paper summary: claim, code citations, SOTA
  reports/              -- audit reports (citation-verifier, peer-review etc.)
```

## Status as of Marathon Run 18 (Mega-Marathon Milestone M3, part 1)

| Bibkey | Title | Status | PDF |
|--------|-------|--------|-----|
| karis2013realshading | Real Shading in Unreal Engine 4 | STAGED | pending |
| heitz2016ltc | Real-Time Polygonal-Light Shading with LTC | STAGED | pending |
| frisvad2012onb | Orthonormal Basis Without Normalization | STAGED | pending |
| eberlylbs | Geometric Tools Skinning (LBS) | STAGED | pending |
| wronski2014volfog | Volumetric Fog | STAGED | pending |

**STAGED** means: notes file exists, BibTeX entry exists with
`note = {DEMIR_KURAL_PENDING_PDF}`, MANIFEST.csv row has the URL and
all metadata except `sha256` / `downloaded_at` / `verified_by`. No new
citation referencing one of these bibkeys may merge until that row flips
to **VERIFIED**.

## How to flip a row from STAGED to VERIFIED

1. Download the PDF from the URL in MANIFEST.csv.
2. `sha256sum research/library/pdf/<filename>.pdf` -> populate the `sha256`
   column.
3. Open the PDF, confirm each item on the per-paper notes file's "Demir
   Kural verification checklist" passes.
4. Update MANIFEST.csv: `status` STAGED -> VERIFIED, fill `verified_by` and
   `downloaded_at`.
5. In bibliography.bib, remove the `note = {DEMIR_KURAL_PENDING_PDF}` line
   from that entry.
6. Commit with message `phase<NNN>-M3: <bibkey> verified -- Demir Kural`.

## Why some entries are STAGED rather than VERIFIED in Run 18

The Run 18 orchestrator session lacked external network access to fetch
the PDFs. The infrastructure (MANIFEST schema, BibTeX, notes templates,
verification checklists) is shipped so a subsequent session with
`academic-researcher` agent (or a human with browser access) can close the
gap in a single sub-phase per paper.

The 5 staged papers were selected because they are **already referenced**
in W7/W8 shader comments and ADRs (W8-AJ-LTC, W8-AN-Karis-MRP, etc.).
Closing the Demir Kural gap on these brings the existing comment
attribution into compliance with the rule.

## Future SOTA-sweep papers

The Mega-Marathon Milestone M3 part 2 (Run 20) involves an
academic-researcher pass to identify newer SOTA that supersedes these 5
anchors. Candidate papers (see each notes file for full discussion):

- Fdez-Aguera 2019 (multi-scattering IBL, supersedes Karis split-sum)
- Heitz et al. 2017 / 2018 stochastic + stratified LTC followups
- Duff et al. 2017 ONB Revisited (supersedes Frisvad)
- Kavan et al. 2007 Dual Quaternion Skinning (supersedes Eberly LBS)
- Hillaire 2016 / 2018 atmospheric sky + temporal volumetrics
  (supersedes Wronski 2014 baseline)

Each candidate gets its own STAGED row when the M3-part-2 pass runs.
