# kavan2007_dual_quaternion_skinning

**Skinning with Dual Quaternions.** Ladislav Kavan, Steven Collins, Jiri Zara, Carol O'Sullivan.
Proceedings of the 2007 Symposium on Interactive 3D Graphics and Games (I3D '07), pages 39--46.
DOI: 10.1145/1230100.1230107.

PDF: research/library/pdf/kavan2007_dual_quaternion_skinning.pdf
SHA-256: e6d12e076d4ddee4206d43bd57d7bda6c5ca2bc14b794332f85b55f2010faeff
Downloaded: 2026-06-07. Source: https://users.cs.utah.edu/~ladislav/kavan07skinning/kavan07skinning.pdf
Verified via: Crossref DOI metadata + Semantic Scholar (356 citations as of 2026-06-07).

---

## Ozet

- **Problem**: Linear blend skinning (LBS) blends joint transformation matrices as a weighted sum
  of rigid matrices, producing "candy-wrapper" collapse artifacts where the blended matrix is no
  longer rigid --- it gains spurious scale and shear components (eq. 1, p.2). Artist workarounds
  exist for character models but fail for cloth and non-humanoid deformables.

- **Yontem**: Replace per-vertex matrix blend with dual quaternion blend (DLB). A rigid
  transformation is encoded as a unit dual quaternion (8 floats, two quaternions) instead of a
  4x3 matrix (12 floats). The weighted blend is computed in dual quaternion space, then
  normalized back to unit length before transforming the vertex position. The result is always a
  valid rigid transformation by construction --- no collapse, no volume loss.

- **Veri / Setup**: Three test models (human arm, camel, cloth). GPU implementation via a vertex
  shader patch; the authors report 197.4 FPS for DQS vs 84.9 FPS for log-matrix blending on the
  same arm model (Figure 1, p.1). The benchmark machine is not fully specified but the 2x-plus
  speedup over log-matrix and parity with LBS on shader cost is the headline result.

- **Sonuc**: DLB eliminates candy-wrapper and flip artifacts seen in LBS and direct quaternion
  blending respectively, at shader cost "comparable to standard linear blending" (p.2). Memory
  per joint drops from 12 to 8 floats. Upgrading an existing LBS system requires only modifying
  the vertex shader and converting matrices to dual quaternions on the CPU upload path.

- **Bizim calismaya baglantiasi**: CHROMODYNAMIC HelloSkinned / cd::anim CPU-LBS path (Marathon
  Run 11, SK1-SK7) uses matrix-weighted LBS. This paper is the canonical peer-reviewed justification
  for DQS as a drop-in upgrade: same rigging data, same bone weight buffer layout, vertex shader
  change only. Cite when documenting why DQS was chosen over LBS in any ADR covering skeletal
  deformation quality. Replaces the unrecoverable `eberlylbs` (ATTEMPTS.md) as the LBS-baseline
  reference.

- **Sayfa referanslari**:
  - Candy-wrapper artifact cause (LBS matrix blend formula): eq. (1), p.2, Section 3.2.
  - DLB algorithm + normalization step: Section 4, p.4.
  - GPU vertex shader complexity claim: p.2, left column, paragraph 2.
  - 8-float memory efficiency vs 12-float matrix: p.2, left column, paragraph 2.
  - Performance comparison figure: Figure 1, p.1 (197.4 FPS DQS vs 84.9 FPS log-matrix).

---

## 3 Alintilanabilir Claim (citation-verifier dogrulamasi icin)

**Claim 1** (GPU cost parity, p.2):
> "dual quaternions can be elegantly computed in a vertex shader with complexity comparable to
> standard linear blending."

**Claim 2** (memory efficiency, p.2):
> "Dual quaternions are more memory efficient, requiring only 8 floats per transformation
> (essentially, two regular quaternions), instead of the 12 required by matrices."

**Claim 3** (LBS collapse mechanism, p.2 / eq.1):
> "the blended matrix [sum wi*Cji] is no longer a rigid transformation, but a general affine one
> (potentially containing scale and shear factors)."

---

## Mendeley Notes

(Not imported from Mendeley -- this paper was acquired directly via author hosting on cs.utah.edu.)

---

## Iliski

- **kavan2008_dqs** (MANIFEST VERIFIED): The 2008 ACM TOG extended version with approximate DQS
  (ScLERP). The 2007 I3D paper is the original conference version; the 2008 paper adds the
  ScLERP approximation and full mathematical proofs. Both are now in the library.
- **eberly_lbs_skinning** (ATTEMPTS.md STAGED/PENDING_PDF): The blocked Eberly LBS engineering
  note. This paper supersedes it as the canonical LBS-baseline citation.
