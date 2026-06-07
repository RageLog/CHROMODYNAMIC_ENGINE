# research/library/ATTEMPTS.md

Mega-Marathon A1 (Run 29 / phase372) — PDF download resolution log for
the 5 Demir Kural pilot papers staged in MANIFEST.csv. Verified PDFs
land in `pdf/<bibkey>.pdf` with status=VERIFIED. Entries below capture
the failed-fetch trail for the papers that did NOT verify in this run.

Timestamp: 2026-05-29T15:33Z

## VERIFIED (4 / 5) — updated Run 30 / phase375 (2026-05-29)

- `karis2013_real_shading_ue4` — direct fetch from
  `cdn2.unrealengine.com`, 2.95 MB, PDF v1.6 zip-deflate. SHA-256
  `5af943f7f978a03a15c234cef623c05d9c0b62d44292511619b762b0436751e6`.
- `frisvad2012_onb_no_normalization` — direct fetch from
  `backend.orbit.dtu.dk` (DTU author hosting), 343 KB, PDF v1.5,
  10 pages. SHA-256
  `ff84deadf8fa4a249f8db16906e484eec902fac671bfadceba0a57bc7823f2ea`.
- `wronski2014_volumetric_fog` — direct fetch from
  `bartwronski.files.wordpress.com`, 5.78 MB, PDF v1.5,
  67 pages. SHA-256
  `93d46671eee4b51c2c6efe8d7919d22b47c5e5eb41ef19c8e2fe37d7f63201f9`.
- `heitz2016_ltc_area_lights` — RESOLVED in Run 30 (2026-05-29). See
  RESOLVED section below. SHA-256
  `d89ed57d4b55f5ca4029970c07cc33d81769a14b5c3321d5c3e117abe76b9511`.

## RESOLVED — Run 30 (phase375)

### heitz2016_ltc_area_lights — VERIFIED 2026-05-29T17:45Z

**Run 29 failure trail:**

1. The wordpress page `https://eheitzresearch.wordpress.com/415-2/` renders
   but the PDF link is a Google Drive link (JavaScript-driven), not a direct
   PDF URL. curl cannot exercise JS handlers.
2. `eheitzresearch.files.wordpress.com/2016/07/ltc_paper.pdf` and
   `/2017/07/ltc_paper.pdf` — both 404.
3. `hal.science/hal-02155101/document` — login wall.
4. Semantic Scholar cache (`pdfs.semanticscholar.org/1531/...pdf`) —
   fetched successfully but was a 43-page slides PDF (16:9 format,
   "Eric Heitz at al. 2016 SIGGRAPH" cover), not the 8-page journal paper.
   Deleted immediately per Demir Kural sanity check.

**Run 30 resolution:**

WebFetch on the author page extracted the Google Drive file IDs directly
from page HTML. Paper file ID: `0BzvWIdpUpRx_d09ndGVjNVJzZjA`. Direct
download URL: `https://drive.google.com/uc?export=download&id=0BzvWIdpUpRx_d09ndGVjNVJzZjA`.

Downloaded 32.8 MB (large due to high-res figures). Verified:

- `file`: `PDF document, version 1.5`
- `pdfinfo` Title: "Real-Time Polygonal-Light Shading with Linearly Transformed Cosines"
- `pdfinfo` Author: "Eric Heitz, Jonathan Dupuy, Stephen Hill, David Neubelt"
- `pdfinfo` Creator: "LaTeX acmsiggraph.cls (11/2015)" — confirms it is the actual paper, not slides
- `pdfinfo` Pages: 8
- DOI footer on page 1: `http://dx.doi.org/10.1145/2897824.2925895`
- MANIFEST.csv updated to VERIFIED; bibliography.bib PENDING\_PDF note removed.

## STAGED — fetch failed, final verdict (1 / 5)

### eberly\_lbs\_skinning -- FINAL: NOT RECOVERABLE

**Run 29 failure:** The original URL `https://www.geometrictools.com/Documentation/Skinning.pdf`
returns a 2094 B HTML "File Not Found" page. The geometrictools.com site was reorganised for GTE
(Geometric Tools Engine) versions 4-6 and the older `Documentation/` path is dead.
The parent directory listing returns 403 Forbidden.

**Run 30 exhaustive retry** -- all of the following returned 404 or HTML:

- `geometrictools.com/Documentation/Skinning.pdf`
- `geometrictools.com/Documentation/SkinDecomposition.pdf`
- `geometrictools.com/Documentation/LinearBlendSkinning.pdf`
- `geometrictools.com/Documentation/SkinMeshes.pdf`
- `geometrictools.com/Documentation/SkinningAnimation.pdf`
- `geometrictools.com/Documentation/SkinnedMesh.pdf`
- `geometrictools.com/Documentation/SkinningWithDualQuaternions.pdf`
- `geometrictools.com/Samples/Graphics/Skinning/Skinning.pdf`
- GitHub raw: `davideberly/GeometricTools/master/Documentation/Skinning.pdf` -- 404
- Wayback Machine CDX API: connection timeout (web.archive.org unreachable from this shell)
- Wayback direct snapshots (2015, 2018): both returned 142 KB HTML shell -- document was never
  archived as a live PDF at that URL.

**Conclusion:** The Eberly "Skinning" engineering note is not recoverable via open-access means.
`eberly_lbs_skinning` remains STAGED / PENDING\_PDF. The bibkey `eberlylbs` is blocked from
citation. Recommended substitute: Kavan et al. 2007 "Skinning with Dual Quaternions" -- free
access, peer-reviewed, covers LBS as its explicit baseline. Acquire that PDF via a fresh
`academic-researcher` session before citing LBS theory in any ADR or shader comment.

## Demir Kural status -- updated 2026-06-07

`heitz2016ltc` -- VERIFIED as of 2026-05-29. May be cited freely in ADRs and shader comments.

`eberlylbs` -- STAGED / PENDING_PDF. Citation still blocked. Substitute acquired below.

`kavan2007_dual_quaternion_skinning` -- VERIFIED as of 2026-06-07. Fully replaces `eberlylbs`
as LBS-baseline reference in skinning ADRs and shader comments. DOI: 10.1145/1230100.1230107.
SHA-256: e6d12e076d4ddee4206d43bd57d7bda6c5ca2bc14b794332f85b55f2010faeff.

## eberlylbs substitute -- CLOSED

The substitute recommended in the Run 30 verdict has been acquired:

- bibkey: `kavan2007_dual_quaternion_skinning`
- BibTeX anchor: `kavan2007dqs`
- PDF: research/library/pdf/kavan2007_dual_quaternion_skinning.pdf (8 pages, 4.5 MB)
- Source: [kavan07skinning.pdf](https://users.cs.utah.edu/~ladislav/kavan07skinning/kavan07skinning.pdf) (author hosting)
- Verified: pdftotext title match, Crossref DOI metadata, Semantic Scholar 356 citations.
- MANIFEST row: status=VERIFIED, downloaded_at=2026-06-07T14:36:24Z.

Any ADR or shader comment that previously referenced `eberlylbs` for LBS theory should now
cite `kavan2007dqs` instead. The Kavan 2007 I3D paper covers LBS as its explicit baseline
(Section 3.2, eq.1) and is open-access, peer-reviewed, and fully citable.

## Citation-verifier handoff -- eberlylbs only

When a future session re-attempts this paper (if ever needed), the verifier must confirm:

1. PDF magic bytes (`file pdf/BIBKEY.pdf` reports `PDF document`).
2. SHA-256 matches a recomputed digest.
3. The notes/BIBKEY.md claim text quotes appear in the PDF body.
4. BibTeX `note = {DEMIR_KURAL_PENDING_PDF ...}` is removed and the entry is left clean.

For `kavan2007dqs`: all four criteria are already satisfied as of 2026-06-07.
