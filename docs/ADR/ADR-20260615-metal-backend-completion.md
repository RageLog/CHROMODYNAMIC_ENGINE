# ADR-20260615-metal-backend-completion

> Metal backend'i Vulkan paritesine getirmenin TAM yapisal plani. Mevcut
> `engine/render/rhi/src/metal/*.mm` iskeleti (Sprint 1-5, phase548-649) IDevice
> yuzeyini "27/27 implemented" raporluyor ama bu sayim YANILTICI: create_buffer/
> create_texture stub handle donuyor, lookup_buffer/lookup_texture HARD-CODED nil,
> bind_descriptor_set bos `{}`, barrier bos `{}`, create_shader_module ham MSL text
> bekliyor (M3 toolchain'e baglanmamis), create_graphics_pipeline her cagrida ayni
> hard-coded ucgen PSO'sunu uretiyor. Bu ADR her audit item (M1-M12) icin
> mevcut-stub -> hedef-impl (Metal API cagrilari) + Vulkan-referans + efort + .mm
> dosyasi eslestirmesini sabitler ve M9 (RT) icin **geri-donulemez** mimari karari
> verir. ADR-20260530-metal-backend §1-§8 stratejik plani (Fork-A native,
> READY-TO-START verdict) BU ADR tarafindan **uygulanir**; o ADR "ne/neden", bu ADR
> "nasil/hangi-sira".

- **Status**: Proposed (tasarim; .mm implementasyonunu yonlendirir — KOD YOK)
- **Date**: 2026-06-15
- **Branch**: dev
- **Deciders**: Cemal TATLI
- **Author**: architect subagent
- **Related**: ADR-20260530-metal-backend (stratejik Metal plani — Fork-A),
  ADR-20260614-d3d12-binding-model (space-per-set karari; §"Sonuclar"da Metal
  set-per-argument-buffer baglantisi), ADR-20260612-x4-d3d12-shader-toolchain
  (D3D12 toolchain — bu ADR'nin .mm parite simetrisi)

---

## Baglam

3-backend parite mega-marathon: **Vulkan TAM** (primary, referans otorite),
**D3D12 KOMPLE** (byte-exact parity — ADR-20260614-d3d12-binding-model +
phase1196 D16 NDC-Y dersi + phase1115-1133 X1-FU-F/X4-B readback parity).
Simdi sira **Metal**. Kullanici Metal KODUNU YAZACAK ama Mac GPU testi
ertelenecek (Win11'de `.mm` derlenmez; CMake `.mm` dosyalarini `APPLE` +
`CD_RHI_METAL_ENABLED` gate'i ardinda tutar — `CMakeLists.txt:182-241`).

### Kanit 1 — "27/27 implemented" sayimi yaniltici (gercek stub yuzeyi)

`MetalDevice.mm` ust-yorumu (satir 156-159) "27/27 implemented, 0 kNotImpl"
banner'i raporluyor. Ama IDevice metodlari **kNotImpl donmemekle** "gercek
GPU isi yapmak" ayni sey DEGIL. Grep ile dogrulanan gercek stub yuzeyi:

| Metod | `.mm` satir | Gercek davranis |
|---|---|---|
| `create_buffer` | `MetalDevice.mm:293-298` | `next_id_` artirip stub handle donuyor; **MTLBuffer YOK**, registry YOK |
| `create_texture` | `MetalDevice.mm:302-307` | Ayni — stub handle, **MTLTexture YOK** |
| `lookup_buffer` | `MetalDevice.mm:1491-1499` | **`return nil;` HARD-CODED** (yorum: "Sprint-3 adds the map here") |
| `lookup_texture` | `MetalDevice.mm:1501-1506` | **`return nil;` HARD-CODED** |
| `bind_descriptor_set` | `MetalInternal.hpp:639` | **bos govde `{}`** — argument buffer hic baglanmaz |
| `barrier` | `MetalInternal.hpp:701-702` | **bos govde `{}`** — hicbir sync/transition yok |
| `create_graphics_pipeline` | `MetalDevice.mm:512-549` | desc COGUNLUKLA YOK SAYILIR; `build_sprint1_triangle_pipeline` her cagrida ayni hard-coded ucgen PSO'su |
| `create_shader_module` | `MetalDevice.mm:397-420` | `desc.code`'u **ham MSL text** sayar (`build_metal_shader_function` -> `newLibraryWithSource:`); GLSL/SPIR-V girdisi M3 toolchain'e BAGLANMAMIS |
| `update_descriptor_set` | `MetalDevice.mm:752-777` | validate-only; `lookup_*` cagrir, encoder'a **yazmaz** ("best-effort" yorum 745-751) |
| `allocate_descriptor_set` | `MetalDevice.mm:634-707` | TEK-slot `MTLArgumentDescriptor` (`MTLDataTypePointer`, arrayLength 1); per-binding layout YOK (yorum 654-658) |

Yani bir gelistirici Mac'te bu backend'i calistirsa: pencere acilir, swapchain
clear olur, **hard-coded ucgen** cizilir; ama ENGINE'in gercek sahnesi
(Sponza, PBR, IBL, golge) ekrana GELMEZ cunku hicbir vertex/index/uniform
buffer GPU'ya ulasmaz (lookup nil), hicbir descriptor set baglanmaz, hicbir
gercek pipeline desc'ten PSO uretilmez.

### Kanit 2 — M3 shader toolchain HAZIR ama .mm'e BAGLANMAMIS

`cd::rhi_metal_shader` (`CMakeLists.txt:250-259`) host-side, KOSULSUZ derlenen
ayri bir kutuphane: `compose_glsl_to_msl` GLSL -> SPIR-V (glslang) -> MSL
(SPIRV-Cross `CompilerMSL`) zinciri (`MetalShaderToolchain.hpp:99-100`). Win11'de
zaten derlenir + test edilir (PURE host-side C++, MTLDevice gerektirmez).

**Kritik bosluk**: `.mm` `cd_rhi_metal` target'i (`CMakeLists.txt:195-204`)
`cd::rhi_metal_shader`'i **link ETMIYOR** ve `MetalDevice.mm` `compose_glsl_to_msl`'i
**cagirmiyor**. `create_shader_module` ham MSL bekliyor. Engine ise GLSL veriyor
(D16 dersi: tek `create_shader_module(kGlsl)` her backend'de calismali —
`CMakeLists.txt:125-129` Vulkan; D3D12 `compile_glsl_to_dxil`). Metal'de bu
zincir KURULMAMIS.

### Kanit 3 — M3 BINDING CONTRACT (phase1197, set-per-argument-buffer)

`MetalShaderToolchain.hpp:41-63` MSL binding sozlesmesini sabitler:
- **Vulkan descriptor set N -> Metal `[[buffer(N)]]` argument buffer**
- resources `[[id(binding)]]` ile argument-buffer struct ICINDE
- **push_constant -> `[[buffer(16)]]`** (`kPushConstantBufferIndex = 16U`,
  set 0/1 araliginin USTUNDE — cakisma yok)

Bu, ADR-20260614-d3d12-binding-model §"Sonuclar" (satir 200-207) ile DOGRUDAN
simetrik: "space-per-set karari Metal'de set-per-argument-buffer'a dogal olarak
tasinir". `.mm` argument-encoder bu sozlesmeyi HONOR ETMELI. Mevcut tek-slot
encoder (`allocate_descriptor_set`, satir 654-666) bunu KIRIYOR.

### Kanit 4 — Vulkan RT modeli: ray-query (inline RT), SBT-pipeline DEGIL

`IDevice.hpp:408-439`: `create_rt_pipeline` + `get_rt_shader_group_handles`
**her backend'de** (Vulkan dahil) `kNotImplemented` donuyor
(`VulkanDevice.cpp:409-414` stub). Yani Vulkan'in **kendisi de** SBT-pipeline
(raygen/miss/hit + shader binding table) yolunu IMPLEMENTE ETMEMIS.

Vulkan'in IMPLEMENTE ETTIGI sey: **acceleration structure build** (BLAS/TLAS)
komut-buffer tarafinda — `VulkanDevice.cpp:3823-4073` `VkAccelerationStructureKHR`
+ `vkCmdBuildAccelerationStructures` (`ICommandBuffer::build_acceleration_structure`,
`ICommandBuffer.hpp:256`). Sahne shader'lari TLAS'i **ray-query** ile tuketiyor:
`accelerationStructureEXT` + `rayQueryEXT` GLSL'de
(`samples/.../prim.frag.glsl`, ADR-20260614-d3d12-binding-model Kanit 1 satir 32
"binding 2 accelerationStructureEXT"). MEMORY "2-bounce ray query", Run 20/21
"InstanceMatGpu is_sphere" — hepsi inline RT (ray query), SBT degil.

**Sonuc**: Metal RT parite hedefi = **Vulkan'in GERCEK RT yolu** = AS build +
ray-query intersection. SBT-pipeline DEGIL (o Vulkan'da da yok). Bu, M9 kararini
sablonladigi icin kritik — asagida sabitlenir.

---

## Karar (Secilen tasarim)

Metal backend'i Vulkan paritesine **dort fazda** getir; her item icin
mevcut-stub -> hedef-impl + Vulkan-referans + efort + .mm-dosyasi sabitlenir.
**Yeni .mm dosyalari**: `MetalRegistry.mm` (M1), `MetalRenderPass.mm` (M2/M8),
`MetalArgumentBuffer.mm` (M4), `MetalBarrier.mm` (M5), `MetalRayTracing.mm`
(M9). `MetalFormat.mm` (paylasilan format helper — M1/M2/M8'in tumu tuketir).
Mevcut `MetalDevice.mm` / `MetalCommandBuffer.mm` / `MetalPipeline.mm` /
`MetalSwapchain.mm` cerrahi olarak duzeltilir.

### Item mapping tablosu (M1-M12)

| Item | Mevcut stub | Hedef Metal impl | Vulkan referans | Efort | .mm dosya |
|---|---|---|---|---|---|
| **M1** registry | stub handle + nil lookup | `newBufferWithLength:` / `newTextureWithDescriptor:` + handle->id<MTL...> map | `VulkanDevice.cpp:991-1027` (vmaCreateBuffer), `1274-1351` (create_texture) | **L** | `MetalRegistry.mm` (yeni) + `MetalDevice.mm` edit |
| **M6** shader | ham MSL bekler | `compose_glsl_to_msl` (M3) -> `newLibraryWithSource:` -> `newFunctionWithName:` (cleansed `main0`) | `VulkanDevice.cpp` create_shader_module kGlsl dali (D16); D3D12 `compile_glsl_to_dxil` | **M** | `MetalDevice.mm` edit + CMake link |
| **M2** pipeline | hard-coded ucgen | `MTLRenderPipelineDescriptor` desc'ten (vertex layout/blend/format) -> `newRenderPipelineStateWithDescriptor:` | `VulkanDevice.cpp` create_graphics_pipeline (VkGraphicsPipelineCreateInfo) | **L** | `MetalPipeline.mm` edit |
| **M8** render-pass | tek-color, depth yok | `MTLRenderPassDescriptor` color[i]+depth, load/store/clear -> `renderCommandEncoderWithDescriptor:` + `MTLDepthStencilState` | `VulkanDevice.cpp` dynamic-rendering / VkRenderingInfo | **M** | `MetalRenderPass.mm` (yeni) + `MetalCommandBuffer.mm` edit |
| **M4** descriptors | tek-slot encoder + bos bind | per-set `MTLArgumentEncoder` ([[buffer(N)]] layout'tan) + `[[id(binding)]]` encode + `setVertex/FragmentBuffer` slot N + push `setVertex/FragmentBytes` [[buffer(16)]] | `VulkanDevice.cpp:1303-1435` (DSL/layout), update_descriptor_set | **XL** | `MetalArgumentBuffer.mm` (yeni) + `MetalDevice.mm`/`MetalCommandBuffer.mm` edit |
| **M4-Y** NDC-Y | yok | viewport negatif-height VEYA clip-space Y-flip (Vulkan-Y-down esitleme) | phase1196 D16 (D3D12 negatif-height-viewport) | **S** | `MetalCommandBuffer.mm` edit |
| **M5** barriers | bos `{}` | `MTLFence` / `memoryBarrierWithScope:` (untracked heap kaynaklari icin) | `VulkanCommandBuffer.cpp` barrier (vkCmdPipelineBarrier2) | **M** | `MetalBarrier.mm` (yeni) + `MetalCommandBuffer.mm` edit |
| **M7** swapchain | CAMetalLayer var; resize/HDR yok | `nextDrawable`/`present` var; resize/out-of-date + EDR (HDR) ekle | `VulkanDevice.cpp` create_swapchain + acquire/present | **S** | `MetalSwapchain.mm` edit |
| **M12** windowed host | yok (layer disaridan) | `NSWindow`/`NSView` + `CAMetalLayer` host (hello sample) | platform layer (`cd::platform`) | **M** | sample-side (`.mm`) |
| **M9** RT | `kInvalidArgument` | **AS build + ray-query** (asagidaki karar): `MTLAccelerationStructure` BLAS/TLAS + MSL ray_query intersector | `VulkanDevice.cpp:3823-4073` build_acceleration_structure (ray-query yol) | **XL** | `MetalRayTracing.mm` (yeni) |
| **M10** mesh shader | `kNotImplemented` | `MTLMeshRenderPipelineDescriptor` (object/mesh stage) | `create_mesh_pipeline` (Vulkan VK_EXT_mesh_shader) | **L** | (Metal-native, ileride) |
| **M11** bindless | tier yok | argument buffer arrays (tier2) + ResidencySet | `VulkanDevice.cpp` bindless (W8-BE, descriptor_indexing) | **L** | (Metal-native, ileride) |

### M1 — Resource registry (efort: L)

Mevcut: `create_buffer`/`create_texture` (`MetalDevice.mm:293-307`) stub handle;
`lookup_buffer`/`lookup_texture` (`1491-1506`) `return nil` HARD-CODED.

Hedef:
- `create_buffer`: `[mtl_device_ newBufferWithLength:desc.size options:<storage>]`.
  Storage mode mapping (`BufferDesc::memory_usage` -> `MTLResourceStorageMode*`):
  `kDeviceLocal`/`kGpuOnly` -> `Private`; `kHostVisible`/`kUpload` -> `Shared`
  (Apple Silicon unified) veya Intel'de `Managed`; `kReadback` -> `Shared`.
  Vulkan referans: `map_vma_usage` (`VulkanDevice.cpp:104-130`).
- `create_texture`: `MTLTextureDescriptor` (`pixelFormat` via `MetalFormat.mm`,
  `width/height/depth`, `mipmapLevelCount`, `arrayLength`, `usage` =
  `MTLTextureUsageShaderRead|ShaderWrite|RenderTarget` `desc.usage`'tan,
  `storageMode` = `Private` cogunlukla) -> `[device newTextureWithDescriptor:]`.
- Yeni registry: `mutable std::mutex buffers_mu_` + `std::unordered_map<u32,
  MetalBufferObj>` (ve textures). `lookup_buffer`/`lookup_texture` bu maplere
  baglanir. **Bunu yapinca M5-disi her sey otomatik aydinlanir**: copy_buffer,
  bind_vertex_buffer, draw_indexed, upload_buffer, copy_buffer_to_image hepsi
  "graceful skip" yerine gercek is yapar (yorumlardaki "Sprint 3 lights up
  automatically" tam olarak budur).

### M6 — Shader module (efort: M)

Mevcut: `create_shader_module` (`MetalDevice.mm:397-420`) ->
`build_metal_shader_function` (`MetalPipeline.mm:290-384`) `desc.code`'u ham MSL
sayar.

Hedef:
1. `cd_rhi_metal` CMake target'ina `cd::rhi_metal_shader` link et
   (`CMakeLists.txt:203` `PUBLIC_DEPS`'e ekle).
2. `create_shader_module`: `desc.language` dallandirmasi (D16 simetrisi):
   `kGlsl` -> `compose_glsl_to_msl(spirv_compiler, GlslToMslDesc{...})`;
   `kSpirv` -> `compose_spirv_to_msl(...)`; `kMsl` -> ham (mevcut yol).
   MSL `MslArtifact::source` -> `newLibraryWithSource:`,
   `MslArtifact::entry_point` -> `newFunctionWithName:` (SPIRV-Cross `main`'i
   `main0`'a ceviriyor — `MslArtifact::entry_point` ILE lookup, "main" varsayma;
   `MetalShaderToolchain.hpp:86-93`).
3. `MslBindingModel{.argument_buffers=true, .push_constant_buffer_index=16}`
   GECILMELI (M3 contract). Bu, M4 argument-encoder layout'unun shader tarafini
   sabitler.

### M2/M8 — Pipeline + render-pass (efort: L + M)

Mevcut: `create_graphics_pipeline` (`MetalDevice.mm:512-549`) hard-coded ucgen;
`begin_render_pass` (`MetalCommandBuffer.mm:77-149`) tek color, depth yok.

Hedef (M2):
- `MTLRenderPipelineDescriptor`'i GraphicsPipelineDesc'ten kur:
  `vertexFunction`/`fragmentFunction` (M6 modullerinden),
  `vertexDescriptor` (`MTLVertexDescriptor` <- `VertexLayout`: attribute
  format/offset/buffer-index + layout stride/stepFunction),
  `colorAttachments[i]` (pixelFormat + blend: `blendingEnabled`,
  `sourceRGBBlendFactor`/`destination*`/`rgbBlendOperation` +
  alpha analog'lari `BlendState`'ten), `depthAttachmentPixelFormat` +
  `stencilAttachmentPixelFormat`, `rasterSampleCount` (MSAA).
- `MTLDepthStencilState` AYRI obje: `depthCompareFunction` +
  `depthWriteEnabled` (`DepthStencilState`'ten) -> `[device
  newDepthStencilStateWithDescriptor:]`; `[encoder setDepthStencilState:]`.
- Topology: `MTLPrimitiveType` (draw-call'da) + `inputPrimitiveTopology`
  (PSO'da tessellation/restart icin). Cull: `[encoder setCullMode:]` +
  `setFrontFacingWinding:` (`RasterState`'ten).
- Vulkan referans: `VkGraphicsPipelineCreateInfo` (vertex input / raster /
  depth-stencil / color-blend state).

Hedef (M8):
- `begin_render_pass`: `info.color_attachments` UZERINDE dongu (tek degil) ->
  `rpd.colorAttachments[i]`; depth: `info.depth_attachment` ->
  `rpd.depthAttachment` (texture/loadAction/storeAction/clearDepth) +
  `rpd.stencilAttachment`. Color load/store/clear mevcut yol (satir 107-118)
  cok-target'a genisletilir. Render-target view'lar M1 sonrasi gercek
  MTLTexture'lara cozulur (sadece swapchain drawable degil).

### M4 — Descriptors = argument buffers (efort: XL) — KRITIK

Mevcut: `allocate_descriptor_set` (`MetalDevice.mm:634-707`) TEK-slot encoder;
`update_descriptor_set` (`752-777`) validate-only; `bind_descriptor_set`
(`MetalInternal.hpp:639`) bos `{}`.

Hedef (M3 contract'i HONOR ederek):
1. `allocate_descriptor_set`: layout'un GERCEK binding tablosundan
   (`MetalDescriptorSetLayoutObj`'taki `DescriptorSetLayoutBinding` listesi)
   `[MTLArgumentDescriptor]` dizisi kur — her binding icin `index = binding`,
   `dataType` = tip'ten (`kUniformBuffer/kStorageBuffer` -> `MTLDataTypePointer`;
   `kSampledImage` -> `MTLDataTypeTexture`; `kSampler` -> `MTLDataTypeSampler`),
   `arrayLength` = bindless icin slot_count. -> `newArgumentEncoderWithArguments:`
   -> `[encoder encodedLength]` boyutunda `Shared` argument buffer.
2. `update_descriptor_set`: encoder'a GERCEK yaz —
   `[encoder setBuffer:buf offset:o atIndex:binding]` (UBO/SSBO),
   `[encoder setTexture:tex atIndex:binding]` (sampled image),
   `[encoder setSamplerState:s atIndex:binding]`. `[[id(binding)]]` struct
   icindeki konum = `atIndex:binding`.
3. `bind_descriptor_set(set_index, set)`: argument buffer'i SLOT N'e bagla —
   `[encoder setVertexBuffer:arg_buf offset:0 atIndex:set_index]` +
   `[encoder setFragmentBuffer:... atIndex:set_index]` (compute:
   `setBuffer:atIndex:`). **Vulkan set N = Metal [[buffer(N)]]** (M3 contract).
   Ayrica `[encoder useResource:usage:]` ile argument-buffer'in REFERANS
   ETTIGI her kaynagi resident yap (Metal argument-buffer kurali — aksi halde
   GPU kaynagi goremez; bu, W8-BE bindless cross-encoder visibility dersi
   ADR-20260530 §8.5.1 ile ayni).
4. push_constants: `set_index >= 16` cakismaz; `[[buffer(16)]]`'e
   `setVertex/FragmentBytes` (mevcut `push_constants`,
   `MetalCommandBuffer.mm:360-387` `offset`'i arg-index olarak kullaniyor —
   `kPushConstantBufferIndex=16` SABITINE baglanmali, caller offset'ine degil).

**M4-Y — NDC-Y parity (efort: S, MANDATORY)**: Metal clip-space +Y YUKARI
(D3D12 gibi); Vulkan +Y ASAGI. Ekran goruntusu ters cikmamasi icin
phase1196 D16 dersinin Metal analogu: ya viewport negatif-height
(`MTLViewport.height = -h`, `originY = h`) ya da PSO/MSL clip-space Y-flip.
**Tek backend'de bile atlanirsa cross-backend parite KIRILIR** (D16 dersi
zorunlu kilar). SPIRV-Cross `CompilerMSL` `flip_vertex_y` opsiyonu var —
toolchain (M6) tarafinda mi yoksa viewport tarafinda mi yapilacagi TEK
yerde sabitlenmeli (oneri: viewport negatif-height, cunku shader'i
backend-agnostik tutar; D3D12 ile ayni desen).

### M5 — Barriers (efort: M)

Mevcut: `barrier` (`MetalInternal.hpp:701-702`) bos `{}`.

Hedef: Metal cogu sync'i OTOMATIK yapar (tracked resources, encoder
sinirlari). Manuel barrier sadece **untracked** (heap-placed / argument-buffer)
kaynaklar icin gerekir:
- Ayni encoder ici: `[encoder memoryBarrierWithScope:MTLBarrierScopeBuffers|
  Textures afterStages:beforeStages:]` (render/compute encoder).
- Encoder'lar arasi / queue: `MTLFence` (`[device newFence]`,
  `[encoder updateFence:afterStages:]` / `[encoder waitForFence:beforeStages:]`).
- `BufferBarrier`/`TextureBarrier` -> scope + stage mask cevirisi. Layout
  transition Metal'de YOK (storageMode sabit); sadece visibility/ordering.
- Vulkan referans: `vkCmdPipelineBarrier2` src/dst stage+access mask.

### M7/M12 — Swapchain + windowed host (efort: S + M)

Mevcut: `MetalSwapchainObj` (`MetalSwapchain.mm`) CAMetalLayer + nextDrawable +
present VAR. Eksik: resize (drawableSize guncelle), out-of-date (nextDrawable
nil -> `kSwapchainOutOfDate` mevcut 990-992 — yeterli), HDR/EDR
(`CAMetalLayer.wantsExtendedDynamicRangeContent` + `colorspace` +
`EDRMetadata`). M12: hello sample icin `NSWindow`/`NSView` + `CAMetalLayer`
host (Fork-A; ADR-20260530 §8.5.2 AppKit/UIKit shell delegation).

---

## M9 — Metal RT (GERI-DONULEMEZ KARAR)

### Karar: (a) RHI'nin RT soyutlamasini Metal'e adapte et — ray-query (inline RT) yolu, AYRI Metal-native path DEGIL

Metal RT modeli Vulkan SBT-pipeline'dan YAPISAL OLARAK FARKLI:
`MTLAccelerationStructure` + `MTLIntersectionFunctionTable` +
`visible-function-table` var; **SBT (shader binding table) YOK**. Iki secenek:

- **(a) [SECILEN]** RHI'nin MEVCUT RT yuzeyini Metal'e adapte et. RHI'nin
  GERCEK RT yuzeyi = **acceleration-structure build + ray-query**, SBT-pipeline
  DEGIL. (Kanit 4: `create_rt_pipeline`/`get_rt_shader_group_handles` Vulkan'da
  DAHI stub — `IDevice.hpp:408-439`, `VulkanDevice.cpp:409-414`. Engine TLAS'i
  shader-ici `rayQueryEXT` ile tuketiyor — `ICommandBuffer.hpp:256`
  `build_acceleration_structure` IMPLEMENTE, `VulkanDevice.cpp:3823-4073`.)
  Metal'de bu DOGRUDAN map olur:
  - `create_acceleration_structure` -> `[device
    newAccelerationStructureWithDescriptor:]` (BLAS:
    `MTLPrimitiveAccelerationStructureDescriptor` +
    `MTLAccelerationStructureTriangleGeometryDescriptor`; TLAS:
    `MTLInstanceAccelerationStructureDescriptor`).
  - `ICommandBuffer::build_acceleration_structure` -> `MTLAccelerationStructure
    CommandEncoder` `buildAccelerationStructure:descriptor:scratchBuffer:`.
  - shader: MSL `raytracing` header `metal::raytracing::intersector<...>` +
    `instance_acceleration_structure` — `rayQueryEXT`'in MSL analogu. SPIRV-Cross
    `CompilerMSL` ray-query lowering destekler (M6 toolchain'in MSL ciktisi).
  - argument buffer'a TLAS bagla: `[encoder setAccelerationStructure:atIndex:]`
    (`update_descriptor_set` `kAccelerationStructure` dali — su an
    `kInvalidArgument` donen `MetalDevice.mm:758-766` — burada AYDINLANIR).

- **(b) [REDDEDILEN]** Metal-native SBT-benzeri intersection-function-table
  path'ini RHI'de AYRI bir API olarak ekle. Reddedildi:
  - RHI'de SBT-pipeline yuzeyi **kullanilmiyor** (Vulkan'da bile stub). Olmayan
    bir soyutlamayi Metal'e map etmek = hayali parite. Engine ray-query
    kullaniyor; intersection-function-table (callable/named hit shaders) engine
    sahnesinde TUKETICISI YOK.
  - Ayri Metal path = backend-divergent RT API = parite hedefinin (tek RHI
    yuzeyi, N backend) ihlali. D3D12 de DXR Inline (ray-query) ile ayni RHI
    yuzeyini tutuyor — Metal ucuncu backend olarak ayni yuzeyi tutmali.
  - `MTLIntersectionFunctionTable` + visible-function-table, custom-intersection
    (procedural AABB / sphere-primitive) ICIN ileride GEREKEBILIR (Run 20
    `is_sphere` analitik kuresi su an shader-ici, AS'siz). O zaman bu, M9'un
    DAHILI implementasyon detayi olur (ray-query intersector'a function-table
    eklenir), AYRI RHI API'si DEGIL.

**Gerekce ozeti**: Metal RT'yi RHI'nin GERCEK (ray-query) RT yuzeyine adapte et;
SBT-soyutlamasini Metal'e tasima (cunku o soyutlama Vulkan'da da
implemente DEGIL ve engine onu kullanmiyor). Bu karar, `create_rt_pipeline`
SBT yolunu Metal'de DE stub birakir (parite: Vulkan ile ayni durum) ve gercek
isi `build_acceleration_structure` + ray-query'ye yikar. **Geri-donulemez**
cunku argument-buffer'a TLAS baglama + MSL ray-query intersector secimi
M4 + M6 layout'unu da sabitler; sonradan SBT-path'e gecmek tum descriptor
modelini etkiler.

---

## Reddedilen alternatifler

### A. "27/27 implemented" sayimini parite kabul et, sadece RT ekle
Reddedildi: Kanit 1'deki 10 stub (nil-lookup, bos bind/barrier, hard-coded
pipeline) yuzunden engine sahnesi Metal'de RENDER OLMAZ. Sayim metod-imza
kapsamini olcer, davranis paritesini DEGIL.

### B. MoltenVK uzerinden Metal (Vulkan->Metal transpile)
Reddedildi (ADR-20260530 §2.2 Fork-B): App Store red riski, RT/function-pointer
kismi mapping, "supersede SOTA" bari native Metal ister. Bu ADR Fork-A
(native) uygular.

### C. Shader toolchain'i .mm icinde inline (M3'u baypas et)
Reddedildi: `cd::rhi_metal_shader` zaten host-side hazir + Win11'de test edilebilir.
.mm'de inline = test edilemez (Mac-gated) + D3D12 toolchain simetrisini bozar.
Karar: .mm SADECE `compose_glsl_to_msl` ciktisini TUKETIR (link + cagri).

### D. M9'da SBT-pipeline path (secenek b)
Yukarida M9'da gerekceli reddedildi.

---

## Sonuclar (etkilenen moduller)

- **`engine/render/rhi/CMakeLists.txt:195-204`**: `cd_rhi_metal` target'ina
  yeni .mm dosyalari (`MetalRegistry/RenderPass/ArgumentBuffer/Barrier/
  RayTracing/Format.mm`) + `cd::rhi_metal_shader` PUBLIC_DEP eklenir.
- **`MetalDevice.mm`**: create_buffer/texture (M1), create_shader_module (M6),
  allocate/update_descriptor_set (M4), create_graphics_pipeline (M2),
  lookup_buffer/texture (M1) duzeltilir. Registry uyeleri + mutex'ler eklenir.
- **`MetalInternal.hpp`**: `MetalBufferObj`/`MetalTextureObj` (M1),
  `MetalDepthStencilStateObj` (M2), `MetalAccelObj` (M9) sinif tanimlari +
  `MetalDeviceCtx` lookup metodlari (`lookup_accel`, vb.) eklenir.
- **`MetalCommandBuffer.mm`**: bind_descriptor_set (M4), barrier (M5),
  begin_render_pass cok-target+depth (M8), NDC-Y viewport (M4-Y),
  build_acceleration_structure + ray-query dispatch (M9) eklenir.
- **`MetalPipeline.mm`**: build_sprint1_triangle_pipeline -> gercek
  desc-driven `build_metal_graphics_pipeline` (M2) genisler.
- **Shader corpus**: SPIRV-Cross MSL ciktisi argument-buffer + [[buffer(16)]]
  push + ray-query intersector uretmeli — `cd::spirv_cross_glue` MSL tarafi
  (`compose_glsl_to_msl`) M3 contract'ina (set-per-argument-buffer) zaten
  baglanmis (`MetalShaderToolchain.hpp:41-63`); RT lowering icin `CompilerMSL`
  ray-query opsiyonu dogrulanmali.
- **Test (Mac-gated)**: `chrome_sponza_baseline` golden fixture
  (ADR-20260530 §8.5.5) Metal'de Vulkan ile FLIP/SSIM parite olculur. .mm-disi
  M3 toolchain testleri Win11'de KOSAR (MSL-string golden: set-per-arg-buffer
  + [[buffer(16)]] + ray-query intersector uretildigini assert eden golden).
- **macOS BUILD/CI**: Kullanici Mac'te `cmake --preset <mac> -DCD_RHI_METAL_
  ENABLED=ON` ile .mm derler; `ctest` Metal GPU testlerini (golden diff)
  kosar. Win11'de `CD_RHI_METAL_ENABLED=OFF` (default) — .mm derlenmez, stub
  `MetalDevice.cpp` linklenir; `cd::rhi_metal_shader` testleri (M3, host-side)
  HER PLATFORMDA kosar.

## Implementasyon sirasi (developer kontrati)

**Sira KRITIK** — foundation once, bagimliliklar yukari akar:

1. **M1 + M6 (foundation, paralel)**: registry + shader toolchain link. Bunlar
   olmadan hicbir gercek kaynak/pipeline uretilemez. M1, M2/M4/M5/M7'nin
   tumunun on-kosulu (lookup'lar gercek MTLObject doner). M6, M2'nin on-kosulu
   (gercek vertex/fragment function).
2. **M2 + M8 + M4 (sahne render)**: desc-driven pipeline + cok-target/depth
   render-pass + argument-buffer descriptors. M4-Y (NDC-Y) M2 ile birlikte
   (PSO/viewport ayni yerde). Bu uc item engine sahnesini (Sponza/PBR/IBL/
   golge) ekrana getirir.
3. **M5 + M7 (sync + present polish)**: barriers (untracked kaynaklar
   aydinlaninca gerekir) + swapchain resize/HDR.
4. **M9 (RT)**: AS build + ray-query. M1 (buffer'lar), M4 (TLAS argument-buffer
   binding), M6 (ray-query MSL) HEPSI on-kosul — bu yuzden EN SON.
5. **M10/M11 (Metal-native, opsiyonel)**: mesh shader + bindless tier2 —
   parite-otesi; ayri faz.

**Invariantlar**: (i) M3 contract — set N -> [[buffer(N)]], push -> [[buffer(16)]]
HER pipeline'da; (ii) NDC-Y — viewport negatif-height TEK yerde, tum backend'ler
ayni ekran ciktisi; (iii) M9 — SBT-pipeline yolu Metal'de DE stub (Vulkan
paritesi), gercek RT ray-query; (iv) .mm SADECE `compose_glsl_to_msl` ciktisini
tuketir, shader-compile mantigini inline ETMEZ.

**Implementasyonu kim yapmali**: `developer` (Mac-gated .mm) — bu ADR yapisal
kontrat; M3 toolchain (host-side) parcalari Win11'de simdi test edilebilir,
.mm parcalari Mac'te. Architect refactor'a girmez.
