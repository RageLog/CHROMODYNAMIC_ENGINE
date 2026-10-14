#!/usr/bin/env python3
"""Scaffolds a compact Spec-Driven Development (SDD) Micro-Spec."""
from __future__ import annotations

import argparse
import datetime
import re
import sys
from pathlib import Path


TEMPLATE = """# SPEC: {title}

- **Date**: {date}
- **Module**: `{module}`
- **Layer**: `{layer}`
- **Scope**: `{mode}`

## 1. Functional Goal
{intent}

## 2. Layer & Blast Radius
- **Target Path**: `engine/{domain}/{module}/`
- **Allowed Dependencies**: Layers strictly `< {layer}`

## 3. C++23 API Contract Sketch
```cpp
namespace cd::{ns} {{
    // [[nodiscard]] public API using std::expected / std::span / constexpr
}} // namespace cd::{ns}
```

## 4. Acceptance Test Matrix (Red -> Green)
- [ ] **[Happy]**: Normal input produces expected deterministic output.
- [ ] **[Edge]**: Empty / boundary / max-capacity input handled safely.
- [ ] **[Negative]**: Invalid state or argument returns `std::unexpected` without UB.

## 5. Verification Command
```powershell
powershell -ExecutionPolicy Bypass -File "C:/Users/cemal/.gemini/config/plugins/argon-core/skills/tdd-red-green-loop/scripts/run_tdd_cycle.ps1" -Target {module}_tests
```
"""


def slugify(text: str) -> str:
    return re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-")


def main() -> int:
    parser = argparse.ArgumentParser(description="Scaffold a compact Spec-Driven Development (SDD) markdown contract.")
    parser.add_argument("--title", required=True, help="Feature or subsystem title")
    parser.add_argument("--module", required=True, help="Target module name (e.g. cd_spatial_hash)")
    parser.add_argument("--domain", default="foundation", help="Engine domain directory (foundation/render/world/ui/game)")
    parser.add_argument("--layer", default="L2", help="DAG Layer (L0..L6)")
    parser.add_argument("--mode", default="Production", choices=["VerticalSlice", "Production"], help="Execution scope")
    parser.add_argument("--intent", default="Deliver a deterministic, zero-overhead C++23 implementation matching the specification.", help="1-sentence goal")
    parser.add_argument("--out-dir", default="docs/specs", help="Output directory for specs")
    args = parser.parse_args()

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    slug = slugify(args.title)
    out_file = out_dir / f"SPEC-{slug}.md"

    ns = args.module.removeprefix("cd_")
    content = TEMPLATE.format(
        title=args.title,
        date=datetime.date.today().isoformat(),
        module=args.module,
        layer=args.layer,
        mode=args.mode,
        intent=args.intent,
        domain=args.domain,
        ns=ns,
    )
    out_file.write_text(content, encoding="utf-8", newline="\n")
    print(f"SPEC_CREATED: {out_file.as_posix()} (module={args.module}, layer={args.layer}, scope={args.mode})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
