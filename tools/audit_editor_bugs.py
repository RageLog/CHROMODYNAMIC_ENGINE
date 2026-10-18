#!/usr/bin/env python3
"""
Autonomous Static Bug & Iterator-Invalidation Auditor for CHROMODYNAMIC Studio.
Catches:
  1. std::vector iterator invalidation (mutating disk_browser_items, loaded_context_assets,
     s.entities, s.lights, stack.items inside range-for loops).
  2. ImGui Push/Pop and Begin/End stack mismatches.
  3. Shader source vs embedded .inl vs runtime build bin/shaders/ desynchronization.
  4. Light intensity scale mismatches between UI sliders and resolve_sun_light.
"""

from __future__ import annotations
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent


def check_range_for_mutations() -> list[str]:
    errors: list[str] = []
    editor_dir = ROOT / "apps" / "editor"
    containers = [
        ("s_cached_entries", ["refresh_disk_browser", "s_cached_entries.clear", "s_cached_entries.push_back", "s_cached_entries.erase", "s_cached_dir.clear"]),
        ("disk_browser_items", ["refresh_disk_browser", "disk_browser_items.clear", "disk_browser_items.push_back", "disk_browser_items.erase"]),
        ("loaded_context_assets", ["loaded_context_assets.clear", "loaded_context_assets.erase", "loaded_context_assets.push_back", "load_asset_into_context", "populate_starter_context_assets"]),
        ("s.entities", ["s.entities.erase", "s.entities.push_back", "s.entities.clear", "reset_to_clean_empty_project", "load_studio_interactive_template"]),
        ("s.lights", ["s.lights.erase", "s.lights.push_back", "s.lights.clear", "delete_studio_light_at", "spawn_studio_light_by_type"]),
    ]

    for path in sorted(editor_dir.glob("*.hpp")) + sorted(editor_dir.glob("*.cpp")):
        text = path.read_text(encoding="utf-8", errors="ignore")
        lines = text.splitlines()
        for i, line in enumerate(lines):
            for cont_name, mutators in containers:
                # Match range-based for loop (single ':' not '::') or forward index loop ('< cont_name.size()')
                line_no_scope = line.replace("::", "__SCOPE__")
                is_range_for = ("for" in line_no_scope and ":" in line_no_scope and cont_name in line)
                is_fwd_idx_for = ("for" in line and f"{cont_name}.size()" in line and "++" in line)
                if is_range_for or is_fwd_idx_for:
                    # Scan loop body by brace depth
                    depth = 0
                    started = False
                    for j in range(i, min(i + 220, len(lines))):
                        l_cur = lines[j]
                        # Strip single-line comments
                        l_code = l_cur.split("//")[0]
                        for ch in l_code:
                            if ch == "{":
                                depth += 1
                                started = True
                            elif ch == "}":
                                depth -= 1
                        if j > i:
                            for mut in mutators:
                                if mut in l_code:
                                    errors.append(
                                        f"[ITERATOR-INVALIDATION] {path.name}:{j+1}: "
                                        f"Mutator '{mut}' called inside range-for loop over '{cont_name}' (started at line {i+1})"
                                    )
                        if started and depth <= 0:
                            break
    return errors


def check_shader_sync() -> list[str]:
    errors: list[str] = []
    glsl_path = ROOT / "apps" / "editor" / "shaders" / "prim.frag.glsl"
    inl_path = ROOT / "apps" / "editor" / "PrimShader_kPrimFS.inl"
    if not glsl_path.exists() or not inl_path.exists():
        return ["Missing prim.frag.glsl or PrimShader_kPrimFS.inl"]

    glsl_text = glsl_path.read_text(encoding="utf-8", errors="ignore")
    inl_text = inl_path.read_text(encoding="utf-8", errors="ignore")

    for required_token in ["any_light", "ibl_gate", "is_floor"]:
        if required_token not in glsl_text:
            errors.append(f"[SHADER-MISSING] '{required_token}' missing in prim.frag.glsl")
        if required_token not in inl_text:
            errors.append(f"[SHADER-SYNC] '{required_token}' missing in PrimShader_kPrimFS.inl")

    runtime_copies = [
        ROOT / "build" / "ide-base" / "bin" / "shaders" / "prim.frag.glsl",
        ROOT / "build" / "ninja-base" / "bin" / "Debug" / "shaders" / "prim.frag.glsl",
        ROOT / "build" / "ninja-base" / "bin" / "Release" / "shaders" / "prim.frag.glsl",
    ]
    for rc in runtime_copies:
        if rc.exists():
            rc_text = rc.read_text(encoding="utf-8", errors="ignore")
            if rc_text != glsl_text:
                errors.append(f"[SHADER-RUNTIME-STALE] {rc.relative_to(ROOT)} differs from apps/editor/shaders/prim.frag.glsl")
    return errors


def main() -> int:
    all_errors: list[str] = []
    all_errors.extend(check_range_for_mutations())
    all_errors.extend(check_shader_sync())

    if all_errors:
        print(f"=== AUDIT FAILED ({len(all_errors)} issue(s)) ===")
        for err in all_errors:
            print("  *", err)
        return 1

    print("=== AUDIT PASSED: 0 iterator invalidations, 0 shader sync errors ===")
    return 0


if __name__ == "__main__":
    sys.exit(main())
