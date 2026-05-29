# research/library/ATTEMPTS.md

Mega-Marathon A1 (Run 29 / phase372) — PDF download resolution log for
the 5 Demir Kural pilot papers staged in MANIFEST.csv. Verified PDFs
land in `pdf/<bibkey>.pdf` with status=VERIFIED. Entries below capture
the failed-fetch trail for the papers that did NOT verify in this run.

Timestamp: 2026-05-29T15:33Z

## VERIFIED (3 / 5)

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

## STAGED — fetch failed in this run (2 / 5)

### heitz2016_ltc_area_lights

Listed source URL `https://eheitzresearch.wordpress.com/415-2/`. Issues:

1. The wordpress page renders but contains no direct PDF link in the
   parsed HTML. The page links to a code archive
   `http://blog.selfshadow.com/publications/ltc/ltc_demo.zip` and an
   interactive demo, but not the paper PDF.
2. Tried `eheitzresearch.files.wordpress.com/2016/07/ltc_paper.pdf`
   and `/2017/07/ltc_paper.pdf` — both return 404 (1377 B HTML body).
3. Tried `hal.science/hal-02155101/document` — returns login wall
   (12.5 KB HTML).
4. The wordpress page is JavaScript-rendered; the actual PDF download
   button likely sits behind a JS handler that curl cannot exercise.

**Next-session resolution path**: dispatch academic-researcher with
explicit fallback list — (a) re-render page via headless Chromium,
(b) try `eheitzresearch.files.wordpress.com/2017/07/heitz-real-time-polygonal-light.pdf`
or similar variants observed in CDN logs, (c) request via Eric Heitz's
current employer page if reachable.

### eberly_lbs_skinning

Listed source URL `https://www.geometrictools.com/Documentation/Skinning.pdf`.
The page returns a 2094 B HTML "File Not Found" page — the
geometrictools.com site was reorganised for GTE (Geometric Tools
Engine) versions 4-6 and the older `Documentation/` path is dead.
The parent directory listing returns 403 Forbidden.

**Next-session resolution path**: dispatch academic-researcher with
explicit fallback list — (a) crawl `geometrictools.com` for the new
LBS document URL (likely under `/Books/` or `/Samples/`), (b) try the
Wayback Machine snapshot `web.archive.org/web/*/geometrictools.com/Documentation/Skinning.pdf`
(historically reachable), (c) substitute a peer-reviewed LBS reference
such as Magnenat-Thalmann et al. 1988 if Eberly's note is unrecoverable.

## Demir Kural status

Per CLAUDE.md §2 and the Demir Kural rule: no ADR, shader comment, or
TeX source may cite `heitz2016ltc` or `eberlylbs` until those rows
move to status=VERIFIED with non-empty `pdf_relpath` + `sha256`.

Until that happens the LTC area-light commentary in
`engine/render/brdf_ltc/` and the LBS reference in
`engine/world/anim/` must carry inline derivations or cite the
peer-reviewed alternates (Heitz et al. 2016 ACM TOG, Magnenat-Thalmann
1988 Eurographics) WITH MANIFEST.csv update first.

## Citation-verifier handoff

When the next session re-attempts these two papers, the verifier must
re-confirm:

1. PDF magic bytes (`file pdf/<bibkey>.pdf` reports `PDF document`).
2. SHA-256 matches a recomputed digest.
3. The notes/<bibkey>.md claim text quotes appear in the PDF body.
4. BibTeX `note = {DEMIR_KURAL_PENDING_PDF ...}` is removed and the
   entry is left clean.
