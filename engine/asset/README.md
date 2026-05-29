# cd::asset

**Purpose**: shared in-memory asset types -- PrimitiveVertex / PrimitiveMesh / texture buffer descriptors / format enums. Sits beneath every cd::asset_* loader so producer + consumer agree on layout.

**Namespace**: `cd::asset`.

**Headers**: `cd/asset/{Primitives,TextureImage}.hpp`.

**Primary types**:
- `cd::asset::PrimitiveVertex` -- canonical 14-component vertex { pos[3], normal[3], uv[2], color[3], pad }. All sample meshes serialize through this layout so the prim pipeline reads them unchanged.
- `cd::asset::PrimitiveMesh` -- { std::vector<PrimitiveVertex> vertices, std::vector<std::uint16_t> indices }.
- `cd::asset::make_cube` / `make_sphere` / `make_cone` / `make_cylinder` / `make_torus` / `make_plane` / `make_torus_knot` -- procedural mesh generators.
- `cd::asset::TextureImage` -- { width, height, format, std::vector<std::uint8_t> rgba }.

**Test command**: `ctest --preset ninja-debug -R cd_test_asset --output-on-failure`.

**Notes**:
- Header-only.
- 16-bit IB by convention; consumers that need 32-bit IB extend at their layer.
- All cd::asset_gltf / cd::asset_cdmesh / cd::asset_obj loaders bridge their native vertex format into PrimitiveVertex at load.
