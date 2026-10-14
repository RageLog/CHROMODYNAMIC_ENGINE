---
name: visual-quality-loop
description: Audits and validates UI/UX editor panels, HUD widgets, and rendering/shader outputs using automated layout/DPI/state heuristics and golden-image comparison (FLIP/SSIM/PSNR). Automatically triggered when building or modifying UI panels, editor tools, overlays, or rendering features.
---

# Visual Quality Loop — UI/UX Completeness & Render Golden-Diff Protocol

## Core Principle

For editor panels (`engine/ui/editor/panel_*`), HUD widgets (`engine/ui/`), and rendering passes (`engine/render/`), compilation alone is insufficient. Every visual or interactive module must pass structural state completeness and deterministic rendering checks.

## The 4 Quality Gates

1. **Complete State Coverage (Zero Dead UI)**:
   - Every editor panel or interactive widget must handle **Empty**, **Active/Loaded**, **Dirty/Modified**, and **Error/Invalid** states deterministically.
2. **Resolution & DPI Independence**:
   - Avoid hard-coded unscaled pixel magic numbers where proportional layout, theme tokens, or configurable layout constants belong.
3. **Interaction & Affordance Completeness**:
   - Keyboard shortcuts, undo/redo command hooks, pan/zoom/scroll clamping, and clear selection/hover state transitions.
4. **Deterministic Visual / Structural Verification**:
   - For rendering/shader outputs: verify via `cd_imgdiff` (PSNR / SSIM / FLIP golden image comparison) or deterministic buffer readback.
   - For UI/Editor panels: verify state transitions, pan/zoom/hit-testing, and serialization in unit tests.

## Automated UI/UX Quality Audit Command

Run `audit_ui_quality.py` on any UI/Editor module to check test coverage, state handling, and interaction surfaces:

```powershell
python "C:/Users/cemal/.gemini/config/plugins/argon-core/skills/visual-quality-loop/scripts/audit_ui_quality.py" --path engine/ui/editor
```
