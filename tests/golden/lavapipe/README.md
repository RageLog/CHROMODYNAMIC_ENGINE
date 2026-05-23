# tests/golden/lavapipe/

Per-vendor golden reference set for the **Mesa lavapipe** Vulkan
software backend. Wired in Phase 13.B (Wave 153 → v0.31.0).

Lavapipe is the Linux CI's reproducible Vulkan ICD — it runs on every
GitHub Actions Linux runner (no GPU needed) so the engine's Vulkan
path is exercised even when NVIDIA / Intel / AMD silicon is
unavailable. The trade-off is that lavapipe's bit-equality budget is
different from desktop GPU output: arithmetic ordering and edge
filtering produce per-channel deltas of up to ~16/255 vs. the NVIDIA
reference (vs. ≤1/255 between NVIDIA and Intel iGPU per
`docs/ARCHITECTURE.md` §6).

That's why the lavapipe vendor sub-directory is *separate* — it's
not a relaxed-tolerance fallback of the NVIDIA set, it's its own
reference family.

## Why this README is the only file (initial commit)

The capture step requires a real lavapipe run, which only happens on
the Linux GHA runner. To populate the references:

1. Open the **Actions** tab → **CI** workflow → **Run workflow**
2. Set `capture_lavapipe_goldens` = `true`
3. Run on `dev` (or any branch).
4. When the `linux-lavapipe-golden-capture` job finishes, download
   the **linux-lavapipe-goldens** artifact.
5. Unzip the PNG files into this directory on a working copy:
   ```bash
   cd tests/golden/lavapipe/
   unzip -o ~/Downloads/linux-lavapipe-goldens.zip
   ```
6. Sanity-check each PNG visually (size sanity, no all-black frames,
   no validation-layer corruption banners). The auto-capture
   deliberately does NOT auto-commit — operator review is the gate.
7. `git add tests/golden/lavapipe/*.png && git commit -m "..." && git push`.

From that point on, every push to `dev` / `main` will trigger the
`linux-vulkan-sw` job's golden-compare step, which gates against
the committed lavapipe reference set with `--tolerance 16`.

## Re-capture

Re-run the same `workflow_dispatch` whenever a sample's expected
output legitimately changes (new shader, new geometry, etc.). The
PR that introduces the change should land the updated references
in the same commit.

## Tolerance

`scripts/run_golden.sh --tolerance 16` per channel. Lavapipe's
floating-point reduction ordering and (very lightly) different
rasterization tie-break behavior absorb the budget. Tighten only
after a multi-run noise-floor measurement on the actual runner.
