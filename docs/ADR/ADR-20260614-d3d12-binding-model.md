# ADR-20260614-d3d12-binding-model

> Vulkan descriptor-set / binding / push_constant <-> D3D12 HLSL
> register/space eslestirme modeli. ADR-20260612-x4-d3d12-shader-toolchain
> §2.4'te birakilan "binding invariants" sozlesmesini doldurur.

## Baglam

phase1184 GLSL->DXIL zincirini D3D12 `create_shader_module`'a bagladi
(`engine/render/rhi/src/d3d12/D3D12Device.cpp:927-968` GLSL dali ->
`compile_glsl_to_dxil` -> `spirv_to_dxil_tail`). Zincirin ikinci asamasi
SPIRV-Cross ile SPIR-V'yi HLSL'e cevirir
(`engine/render/rhi/src/d3d12/D3D12ShaderToolchain.cpp:81-83`), ucuncu
asama DXC ile DXIL uretir.

Sorun: SPIRV-Cross `CompilerHLSL` su an **hicbir remap olmadan** cagriliyor
(`engine/render/spirv_cross_glue/src/Translate.cpp:82-87` — yalnizca
`shader_model` set ediliyor; `set_root_constant_layouts` /
`add_hlsl_resource_binding` / `set_hlsl_options` kaynak-binding alanlari
bos). Bu yuzden SPIRV-Cross **kendi varsayilan** register/space kuralini
uygular. Bu kural ile D3D12 root signature'in elle yazdigi space duzeni
**iki yerde catisiyor**.

### Kanit 1 — Engine'in gercek binding duzeni (Vulkan tarafi otorite)

Engine GLSL (`samples/engine/hello_engine/shaders/prim.frag.glsl`,
`prim.vert.glsl`, `shadow.vert.glsl`, `line.vert.glsl`):

- **set 0 = per-prim klasik kaynaklar** (prim.frag.glsl):
  - `binding 0` UBO `Shadow` (CBV)            -> b0
  - `binding 1` `sampler2D cd_shadow_map`     -> t1
  - `binding 2` `accelerationStructureEXT`    -> t2 (DXR: AS=SRV)
  - `binding 3` UBO `LightArray`              -> b3
  - `binding 4` `sampler2D cd_albedo_tex`     -> t4
  - `binding 5/6` `samplerCube` IBL spec/diff -> t5/t6
  - `binding 7` `sampler2D cd_brdf_lut`       -> t7
  - `binding 8/9` `sampler2D` normal/mr       -> t8/t9
  - `binding 10` `readonly buffer InstanceMats` -> t10 (readonly SSBO = SRV)
  - `binding 11/12` Sponza VB/IB SSBO         -> t11/t12
  - `binding 14/15` Cesium VB/IB SSBO         -> t14/t15
- **set 1 = ADANMIS bindless seti** (prim.frag.glsl:149-155):
  `layout(set = 1, binding = 0) uniform sampler2D cd_bindless_albedo[];`
  Bilincli olarak set 0'dan ayrildi — yorum (149-154) NVIDIA'nin dinamik-index
  dalini klasik bindings ile ayni sette cokerttuugunu (phase 851/860/864)
  belgeliyor. MEMORY rule 9: "bindless binding MUST live on its own
  descriptor set". Bu set, set 0 ile **birlikte yasamak zorunda** — yani
  iki ayri descriptor set ayni pipeline'da aktif.
- **push_constant `PC`** (prim.vert.glsl:2-13, prim.frag.glsl:15-26):
  mat4 mvp + mat4 model + 9x vec4 = 64+64+144 = 272 B (68 DWORD). shadow/line
  vert'lerde daha kucuk PC bloklari var ama hepsi tek `push_constant`.

Vulkan tarafi bu duzeni dogrudan tuketir: `create_descriptor_set_layout`
her binding'i oldugu gibi alir (VulkanDevice.cpp:1303-1362), bindless
binding `UPDATE_AFTER_BIND | PARTIALLY_BOUND | VARIABLE_DESCRIPTOR_COUNT`
flag'leriyle. `create_pipeline_layout` (1389-1435) `desc.set_layouts`'u
**sirayla** baglar — yani slot 0 -> Vulkan set 0, slot 1 -> Vulkan set 1.
Vulkan'da set INDEX'i = GLSL `set=` degeri.

### Kanit 2 — SPIRV-Cross'un HLSL varsayilani (vendored kaynak)

`build/ninja-base/_deps/spirv-cross-src/spirv_hlsl.cpp`:

- **Kaynak bindings** (`to_resource_register`, satir 3999-4014):
  ```cpp
  if (hlsl_options.shader_model >= 51)
      return join(" : register(", space, binding, ", space", space_set, ")");
  else
      return join(" : register(", space, binding, ")");
  ```
  `space_set` = `get_decoration(var.self, DecorationDescriptorSet)`
  (satir 3946-3947). Yani SM>=51'de SPIRV-Cross **varsayilan olarak
  set N -> space N** uretir. SM6.5 hedefliyoruz
  (D3D12Device.cpp:1009 `kSM6_5`), dolayisiyla set 0 -> space0,
  set 1 -> **space1** emit edilir.
- **Push constant** (`to_resource_register`, satir 4007-4008 +
  `to_resource_binding`/`to_resource_binding_sampler` icin
  `spirv_common.hpp:1907`):
  ```cpp
  static const uint32_t ResourceBindingPushConstantDescriptorSet = ~(0u);
  ...
  if (flag == HLSL_BINDING_AUTO_PUSH_CONSTANT_BIT &&
      space_set == ResourceBindingPushConstantDescriptorSet)
      return "";   // register yok -> cbuffer register'siz
  ```
  `root_constants_layout` bos oldugundan (Translate.cpp set etmiyor),
  push_constant blogu `emit_push_constant_block` ->
  `to_resource_binding(var)` register'SIZ bir `cbuffer` olarak emit edilir.
  DXC bu register'siz cbuffer'i **otomatik `b0, space0`'a** atar.

### Kanit 3 — D3D12 root signature'in elle yazdigi duzen

`engine/render/rhi/src/d3d12/D3D12Device.cpp` `create_pipeline_layout`:

- Her descriptor_set_layout icin **bir descriptor table** uretir
  (`desc.set_layouts` sirasiyla, 1083-1150) — yani set 0 tablo 0, set 1
  tablo 1 olur (set sayisi korunur, bu IYI).
- Ama her range icin **`r.RegisterSpace = 0;` HARDCODE** (satir 1128).
  Yani set 1'in tablosu da space0'da `t0`'dan baslar.
- Push-const root constants: `ShaderRegister = 0` (b0),
  `RegisterSpace = 1` (space1), satir 1184-1185 — "avoid CBV b0 collisions"
  yorumuyla.

### Catisma ozeti (gercek bug yuzeyi)

| Kaynak | SPIRV-Cross emit (default) | Root sig declare | Sonuc |
|---|---|---|---|
| set 0 binding N | `register(x, N, space0)` | tablo space0 register=N | UYUMLU |
| **set 1 binding 0** (bindless) | `register(t, 0, space1)` | tablo **space0** register=0 | **MIS-BIND** (shader space1 ister, root sig space0 verir) |
| **push_constant** | register-siz cbuffer -> DXC `b0, space0` | root constants **b0, space1** | **MIS-BIND** (cbuffer space0/b0 root sig'de yok; b0/space0 ayrica set0 CBV ile cakisir) |

set 0-yalniz + push_constant-yok shader'lar bugun calisir; set 1 (bindless)
veya push_constant kullanan HER shader (yani tum ana prim pipeline'i)
D3D12'de sessizce yanlis baglanir.

## Karar (Secilen tasarim) — space-per-set + acik push_constant remap

**SPACE-PER-SET** secildi. SPIRV-Cross zaten SM6.5'te bu duzeni varsayilan
olarak uretiyor; tek yapmamiz gereken **root signature'i ona uydurmak** ve
**push_constant icin acik bir remap eklemek**.

### Eslestirme kurali (mapping_spec)

**Resource bindings (set, binding) -> (register, space):**
```
HLSL space = Vulkan descriptor set index   (set N -> space N)
HLSL register index = Vulkan binding index  (binding M -> register M)
HLSL register sinifi (b/t/u/s) = kaynak turunden:
  UniformBuffer / UniformBufferDynamic          -> b  (CBV)
  SampledImage / CombinedImageSampler /
    InputAttachment / AccelerationStructure /
    readonly StorageBuffer                       -> t  (SRV)
  StorageImage / read-write StorageBuffer        -> u  (UAV)
  Sampler                                         -> s  (Sampler)
```
Bu, SPIRV-Cross `to_resource_register` varsayilaniyla **birebir ayni**, bu
yuzden translate() tarafinda resource bindings icin EK remap GEREKMEZ.

**Push constant -> root constants:**
```
push_constant PC blogu -> register b0, space1
```
SPIRV-Cross tarafinda bu, `CompilerHLSL::set_root_constant_layouts({{
  .start = 0, .end = <union size>, .binding = 0, .space = 1 }})` cagrisiyla
zorlanmali (veya esdeger `set_hlsl_options` + RootConstants), boylece emit
edilen `cbuffer` `register(b0, space1)` alir ve root signature'in
deklare ettigi `b0/space1` root-constant slotuna oturur.

### Root signature DEGISMELI mi? — EVET, bir satir

- `D3D12Device.cpp:1128` `r.RegisterSpace = 0;` -> **`r.RegisterSpace =
  <bu set_layout'un sirasal index'i>`** olmali (yani N'inci
  `desc.set_layouts` -> space N). Boylece set 1 bindless tablosu space1'de
  deklare edilir ve SPIRV-Cross'un space1 emit'iyle eslesir.
- Push-const slotu (`b0, space1`, satir 1184-1185) **DOGRU** —
  degismez. SPIRV-Cross tarafini buna uyduracagiz (yukarida).
- Tablo-per-set yapisi (her set ayri table) zaten DOGRU; korunur. Bindless
  ayrik set garantisi (MEMORY rule 9) bu tasarimda **dogal olarak korunur**
  — flatten-to-space0 onu KIRARDI.

> NOT: space-per-set'te set 0 CBV `b0` ile push-const `b0` ayni register
> indeksinde ama **farkli space**'te (space0 vs space1), bu yuzden cakisma
> YOK. Root sig'in push-const'u space1'e koyma karari (1185) bu yuzden dogru
> ve korunuyor.

## Reddedilen alternatifler

### A. flatten-to-space0 (her seti space0'a offset'le)
SPIRV-Cross `add_hlsl_resource_binding` ile her (set,binding) icin elle
`{space=0, register=base_offset+binding}` remap'i. **Reddedildi:**
- Collision yonetimi gerekir (set 0 t0..t15 + set 1 t-bindless ayni space0'da
  cakismamali) — elle offset tablosu = kirilgan, her yeni binding'de guncelle.
- **MEMORY rule 9'u KIRAR**: bindless'i klasik bindings ile ayni space'e
  (space0) koyar; bu Vulkan tarafindaki "ayri set" garantisinin D3D12
  semantik karsiligini bozar ve gelecekte D3D12 bindless (SM6.6
  ResourceDescriptorHeap / unbounded table) gecisini zorlastirir.
- SPIRV-Cross'un default'una karsi calismak = her shader icin reflection-driven
  remap insasi; space-per-set ise SIFIR resource-remap ister.

### B. Status quo (remap yok, root sig space0)
Bugunku hal. **Reddedildi:** Kanit 1-3'teki iki mis-bind aktif; bindless +
push_constant kullanan tum prim pipeline'i D3D12'de sessizce bozuk.

### C. set_hlsl_options ile auto-binding kapatip tum bindingleri manuel
Asiri muhendislik; SM6.5 default'u zaten istedigimiz duzen. **Reddedildi.**

## Sonuclar (etkilenen moduller)

- **`engine/render/spirv_cross_glue/src/Translate.cpp`** (`translate_hlsl`,
  82-87): push_constant icin `set_root_constant_layouts({{0, <union>, 0, 1}})`
  eklenmeli. Union boyutu reflection'dan (`get_shader_resources().push_constant_buffers`
  + `get_declared_struct_size`) hesaplanir; root sig'deki `pc_dwords`
  (D3D12Device.cpp:1173) ile **ayni byte araligini** kapsamali.
  `Translate.hpp` API'sine push-const space/binding parametresi eklemek
  gerekebilir (su an parametresiz) — kucuk imza genislemesi.
- **`engine/render/rhi/src/d3d12/D3D12Device.cpp:1128`**: `RegisterSpace = 0`
  -> sirasal set index. Tek satir + dis dongude bir sayac.
- **Test**: X4 D3D12 parity smoke'a set1-bindless + push_constant iceren bir
  shader'in DXIL'inde `register(t0, space1)` ve `register(b0, space1)`
  uretildigini dogrulayan bir golden-string / reflection assert eklenmeli.
- **Metal (gelecek)**: SPIR-V->MSL `CompilerMSL` argument-buffer eslestirmesi
  ayni "set = argument buffer index" prensibini izlemeli — `set N` ->
  `[[buffer(N)]]` argument buffer; push_constant ->
  `ResourceBindingPushConstant{DescriptorSet,Binding}` (spirv_msl.hpp:266-272)
  ile ayri bir buffer slotuna. Yani **space-per-set karari Metal'de
  set-per-argument-buffer'a dogal olarak tasinir**; flatten-to-space0 secseydik
  Metal tarafinda da ek duzlestirme borcu birikirdi. Bu ADR Metal MSL
  mapping'ini de bu yonde baglar.

## Developer kontrati (uygulama ozeti)

1. Translate.cpp `translate_hlsl`: SM>=51 default resource-register'i KORU
   (set N -> space N, binding M -> register M). EK resource remap YAZMA.
2. Translate.cpp `translate_hlsl`: push_constant icin
   `set_root_constant_layouts` ile `register(b0, space1)` zorla; byte araligi
   root sig'in `pc_dwords` hesabiyla ortussun.
3. D3D12Device.cpp:1128: `r.RegisterSpace` = set'in sirasal index'i (0,1,...).
4. Push-const root slot (b0/space1, 1184-1185): DEGISTIRME.
5. Tablo-per-set ve bindless-ayri-set yapisi: KORU.
