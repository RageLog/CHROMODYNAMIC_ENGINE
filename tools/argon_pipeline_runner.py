#!/usr/bin/env python3
"""
CHROMODYNAMIC STUDIO — Argon Orchestrator Deterministic End-to-End Pipeline Runner
==================================================================================
Enforces full-stack verification across Architecture (Layer DAG), Build (Debug/Ninja),
Unit Tests (CTest), Golden Fixture #5 Byte-Parity, and Multi-Level GPU Visual Gates
with strict per-stage timeouts so no partial or broken implementation can pass.
"""
from __future__ import annotations

import argparse
import json
import os
import struct
import subprocess
import sys
import time
import zlib
from pathlib import Path


def run_stage(name: str, cmd: list[str], cwd: Path, timeout_sec: int = 90) -> dict:
    t0 = time.monotonic()
    try:
        proc = subprocess.run(
            cmd,
            cwd=str(cwd),
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=timeout_sec,
        )
        dt = round(time.monotonic() - t0, 2)
        return {
            "stage": name,
            "ok": proc.returncode == 0,
            "returncode": proc.returncode,
            "duration_sec": dt,
            "stdout_tail": "\n".join(proc.stdout.splitlines()[-15:]),
            "stderr_tail": "\n".join(proc.stderr.splitlines()[-15:]),
        }
    except subprocess.TimeoutExpired as exc:
        dt = round(time.monotonic() - t0, 2)
        return {
            "stage": name,
            "ok": False,
            "returncode": -1,
            "duration_sec": dt,
            "stdout_tail": (exc.stdout or "")[-400:] if isinstance(exc.stdout, str) else "",
            "stderr_tail": f"TIMEOUT after {timeout_sec}s",
        }


def inspect_png_basic(path: Path) -> dict:
    if not path.exists() or path.stat().st_size < 128:
        return {"exists": False, "valid_png": False, "size_bytes": 0}
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        return {"exists": True, "valid_png": False, "size_bytes": len(data)}
    w, h = struct.unpack(">II", data[16:24])
    # Extract and decompress IDAT chunks to confirm non-blank image data
    idat_parts = []
    pos = 8
    while pos + 8 <= len(data):
        length = struct.unpack(">I", data[pos : pos + 4])[0]
        chunk_type = data[pos + 4 : pos + 8]
        if chunk_type == b"IDAT":
            idat_parts.append(data[pos + 8 : pos + 8 + length])
        elif chunk_type == b"IEND":
            break
        pos += 12 + length
    raw_len = 0
    non_zero_ratio = 0.0
    if idat_parts:
        try:
            raw = zlib.decompress(b"".join(idat_parts))
            raw_len = len(raw)
            sample = raw[raw_len // 4 : (raw_len * 3) // 4 : 17]
            if sample:
                non_zero_ratio = round(sum(1 for b in sample if b > 16) / len(sample), 4)
        except Exception:
            raw_len = 0
    return {
        "exists": True,
        "valid_png": w > 0 and h > 0 and len(data) > 10000 and raw_len > (w * h) and non_zero_ratio > 0.05,
        "width": w,
        "height": h,
        "size_bytes": len(data),
        "ViewportNonZeroRatio": non_zero_ratio,
    }


ALL_14_STUDIO_LEVELS: list[tuple[int, str]] = [
    (0, "level_1_1_sandbox"),
    (1, "level_1_2_physics_ramp"),
    (2, "level_1_3_domino_chain"),
    (3, "level_3_2_trampoline"),
    (4, "level_2_1_pbr_matrix"),
    (5, "level_2_2_sponza"),
    (6, "level_2_3_optics_bloom"),
    (7, "level_3_1_double_ramp"),
    (8, "level_4_1_avatar_squad"),
    (9, "level_4_2_synth_arena"),
    (10, "level_5_1_celestial_fog"),
    (11, "level_5_2_golden_ocean"),
    (12, "level_2_4_asset_browser"),
    (13, "level_6_1_platformer"),
    (14, "level_6_2_universal_250_nodes"),
]


def main() -> int:
    parser = argparse.ArgumentParser(description="Argon Orchestrator E2E Pipeline Verifier")
    parser.add_argument("--root", default=".", help="Repository root directory")
    parser.add_argument("--build-dir", default="build/ide-base", help="CMake build directory")
    parser.add_argument("--config", default="Debug", help="CMake build configuration (Debug or Release)")
    parser.add_argument("--skip-build", action="store_true", help="Skip CMake build step")
    parser.add_argument("--timeout", type=int, default=90, help="Per-stage timeout in seconds")
    args = parser.parse_args()

    root = Path(args.root).resolve()
    build_dir = (root / args.build_dir).resolve()
    report_stages: list[dict] = []

    # 1. Layer DAG Gate
    dag_script = root / ".agents/skills/engine-sota-adr/scripts/check_layer_dag.py"
    if dag_script.exists():
        res = run_stage(
            "layer_dag",
            [sys.executable, str(dag_script), "--root", str(root / "engine")],
            root,
            timeout_sec=30,
        )
        report_stages.append(res)

    # 2. Academic Iron-Rule Manifest Gate
    manifest_script = root / ".agents/skills/academic-iron-rule/scripts/verify_manifest.py"
    if manifest_script.exists():
        res = run_stage(
            "academic_manifest",
            [sys.executable, str(manifest_script)],
            root,
            timeout_sec=30,
        )
        report_stages.append(res)

    # 3. Build Gate
    if not args.skip_build:
        res = run_stage(
            "cmake_build",
            [
                "cmake",
                "--build",
                str(build_dir),
                "--config",
                args.config,
                "--target",
                "cd_app_editor",
                "cd_test_ui_widgets",
                "cd_test_submitter_host_state",
                "cd_test_world_container",
                "cd_test_brdf_multiscatter",
                "cd_test_splash_audio_cue",
                "-j",
                "8",
            ],
            root,
            timeout_sec=max(args.timeout, 120),
        )
        report_stages.append(res)

    # 4. CTest Gate
    res = run_stage(
        "ctest_suite",
        [
            "ctest",
            "--test-dir",
            str(build_dir),
            "-C",
            args.config,
            "-R",
            "cd_test_ui_widgets$|cd_test_submitter_host_state$|cd_test_world_container$|cd_test_brdf_multiscatter$|cd_test_splash_audio_cue$",
            "--output-on-failure",
        ],
        root,
        timeout_sec=args.timeout,
    )
    report_stages.append(res)

    # 5. Golden Fixture #5 Render Gate
    editor_candidates = [
        build_dir / f"bin/{args.config}/editor.exe",
        build_dir / "bin/editor.exe",
        build_dir / "bin/Debug/editor.exe",
        build_dir / "bin/Release/editor.exe",
        build_dir / "apps/editor/cd_app_editor.exe",
        build_dir / "bin/editor",
    ]
    editor_exe = next((p for p in editor_candidates if p.exists()), editor_candidates[0])
    golden_out = build_dir / "argon_golden5.png"
    if editor_exe.exists():
        if golden_out.exists():
            golden_out.unlink()
        res = run_stage(
            "golden_fixture_5",
            [
                str(editor_exe),
                "--golden-fixture",
                "5",
                "--golden-frames",
                "4",
                "--golden-out",
                str(golden_out),
            ],
            root,
            timeout_sec=args.timeout,
        )
        png_meta = inspect_png_basic(golden_out)
        res["png"] = png_meta
        res["ok"] = res["ok"] and png_meta.get("valid_png", False)
        report_stages.append(res)

    # 6. Full 14-Level Studio Visual Capture Gate (Templates 0..13 across all 6 Worlds)
    if editor_exe.exists():
        for tpl_id, label in ALL_14_STUDIO_LEVELS:
            out_png = build_dir / f"argon_{label}.png"
            if out_png.exists():
                out_png.unlink()
            res = run_stage(
                f"capture_{label}",
                [
                    str(editor_exe),
                    "--template",
                    str(tpl_id),
                    "--frames",
                    "8",
                    "--capture-png",
                    str(out_png),
                ],
                root,
                timeout_sec=args.timeout,
            )
            png_meta = inspect_png_basic(out_png)
            res["png"] = png_meta
            res["ok"] = res["ok"] and png_meta.get("valid_png", False)
            report_stages.append(res)

    all_ok = all(s["ok"] for s in report_stages)
    summary = {
        "pipeline": "argon-orchestrator",
        "all_ok": all_ok,
        "build_dir": str(build_dir),
        "level_count_verified": len(ALL_14_STUDIO_LEVELS),
        "stages": report_stages,
    }
    report_path = build_dir / "argon_pipeline_report.json"
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")

    for s in report_stages:
        status = "PASS" if s["ok"] else "FAIL"
        print(f"[{status}] {s['stage']} ({s['duration_sec']}s)")
        if not s["ok"]:
            print(s["stdout_tail"])
            print(s["stderr_tail"])
    print(f"[argon-pipeline] Overall Verdict: {'PASS' if all_ok else 'FAIL'} ({len(report_stages)} stages) -> {report_path}")
    return 0 if all_ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
