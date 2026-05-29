# cd::asset_obj

**Purpose**: Hand-rolled Wavefront .obj loader. Supports vertices, normals, texcoords, faces, and material references. Self-contained (no tinyobjloader vendoring) because the .obj format is simple enough that a parser is shorter than a wrapper around a third-party library.

**Namespace**: `cd::asset_obj`.

**Public Headers**:
- `cd/asset_obj/ObjLoader.hpp` — OBJ parser; mesh groups, materials, smoothing.
- `cd/asset_obj/AssetLoader.hpp` — async OBJ asset loader interface.

**Primary Types**:
- `ObjMesh` — parsed mesh (vertex position/normal/texcoord arrays, face indices).
- `ObjMaterial` — material definition from .mtl file (Kd, Ks, Ka, Ns, map_Kd texture path).
- `ObjGroup` — named mesh group (name, start face, face count).

**Build**:
```bash
cmake --build --preset ninja-debug --target cd_asset_obj
ctest --preset ninja-debug -R asset_obj --output-on-failure
```

**Dependencies**: cd::core, cd::math.

**Format Support**:
- **Vertices**: position (v), normal (vn), texcoord (vt).
- **Faces**: triangles (f v/vt/vn) and quads (auto-triangulated to v1-v2-v3-v1-v3-v4).
- **Materials**: material names (usemtl), material library (mtllib), smooth groups (s).
- **Comments**: # lines preserved in debug output.

**Notes**:
- OBJ is legacy but widely supported by 3D tools (Blender, Maya).
- For production assets, use glTF (cd::asset_gltf) — better format, smaller files, standard animation support.
- Quads are automatically triangulated (4-vertex faces split to 2 triangles).
- Material paths are relative to .obj file directory.
