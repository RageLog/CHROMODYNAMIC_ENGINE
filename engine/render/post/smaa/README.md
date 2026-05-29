# cd::post_smaa

**Purpose**: SMAA (Subpixel Morphological Anti-Aliasing) post-processing filter. High-quality edge-adaptive MSAA alternative; improves jagged geometry edges and thin geometry without the overhead of multisampling. Produces superior results to FXAA with better texture quality.

**Namespace**: `cd::post_smaa`.

**Public Headers** (Header-only):
- `cd/post_smaa/Smaa.hpp` — SMAA configuration struct (preset quality levels, parameters).

**Primary Types**:
- `SmaaConfig` — preset enum (LOW, MEDIUM, HIGH, ULTRA); controls edge detection threshold.

**Build**:
```bash
cmake --build --preset ninja-debug --target cd_post_smaa
ctest --preset ninja-debug -R post_smaa --output-on-failure
```

**Dependencies**: cd::core, cd::math.

**Algorithm** (Jorge Jimenez):
1. **Edge Detection Pass**: detect edges using luma gradients.
2. **Blending Weight Calculation**: compute blend weights along detected edges.
3. **Neighborhood Blending**: apply weights to blend neighborhood pixels, smoothing jagged edges.

**Rendering** (Pipeline Integration):
- Input: LDR scene color (post-tonemap).
- Output: anti-aliased LDR scene color.
- Two compute shaders or fullscreen passes (configurable).
- 3-tap neighborhood sampling along edges; cheap blend calculation.

**Quality Presets**:
- **LOW**: minimal edge detection (fast, visible aliasing on thin features).
- **MEDIUM**: balanced (good quality at 1080p).
- **HIGH**: aggressive edge detection (slower, superior visual quality).
- **ULTRA**: maximum quality (use on static/replayed footage).

**Notes**:
- Header-only; shader implementation in render/post_fx.
- GPU-bound; typical cost: 1–2ms at 1080p on modern GPUs.
- Edges detected using edge texture (rendered as separate pass or reused from earlier G-Buffer).
- Alternative to MSAA: SMAA is 2–4x cheaper than 2x MSAA, quality comparable.
