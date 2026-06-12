# ADR-20260612 — X4-A D3D12 Shader Toolchain (tek-kaynak GLSL → SPIR-V → SPIRV-Cross HLSL → DXC DXIL)

- **Status**: Proposed → recommended-for-acceptance (X4-A karar düğümü; ROADMAP_PHASE_2 §6 Q1 kullanıcı sign-off'u bekler — öneri NETtir: Seçenek A+DXC)
- **Date**: 2026-06-12
- **Branch**: dev
- **Deciders**: Cemal TATLI
- **Author**: architect subagent
- **Related**: docs/ROADMAP_PHASE_2.md §2.2 madde 1 + §2.3 X4-A düğümü + §6 Q1,
  ADR-016 (vendor matrix; DXC = K1, SPIRV-Cross = K1),
  ADR-20260608-x5-shader-on-disk-hot-reload §2.1 (glslang tek in-process toolchain — bu ADR onu BOZMAZ),
  ADR-20260529-X4-d3d12-parity-status (parity ~%73 audit),
  ADR-20260524-wave168-v0.44.0-phase15c-dxc-sm6 (DXC SM6 entegrasyonu),
  ADR-20260530-metal-backend §1.5 (aynı spirv_cross_glue üzerinden MSL).
- **Scope guard**: Bu ADR yalnız karar + header kontratı tanımlar. `.cpp` İMPLEMENTASYONU bu ADR'den ÇIKMAZ — X4-C implementer (developer ajan) aşağıdaki kontratlarla bağlıdır ve mevcut header'ları `Edit` ile genişletir, paralel yenisini açmaz.

---

## 1. Bağlam (Context)

### 1.1 Karar sorusu

D3D12 backend'inin shader yolu bugüne dek **tanımsızdı** (ROADMAP §2.2 madde 1:
"current GLSL → DXIL path is undefined"). Vulkan yolu GLSL → glslang → SPIR-V
olarak oturmuşken, D3D12 örnekleri (`hello_d3d12_triangle`, `hello_d3d12_pbr`)
elle yazılmış inline HLSL adalarıyla idare ediyor. `hello_d3d12_pbr` baş
yorumu durumu açıkça itiraf eder: *"REAL PBR (M4G-V2) lands when SPIRV-Cross
is plumbed into cd::rhi_d3d12 shader creation path"*
(`samples/render/hello_d3d12_pbr/main.cpp:29-30`).

### 1.2 Ağaçta BUGÜN ne var (kanıt haritası)

Karar bir boşlukta verilmiyor — zincirin üç halkasından ikisi zaten gemide:

| Halka | Durum | Kanıt |
| --- | --- | --- |
| GLSL → SPIR-V | **GEMİDE** | `cd::shader::ICompiler` + glslang backend (`engine/render/shader/include/cd/shader/Compiler.hpp:158`); glslang FetchContent `vulkan-sdk` tag, `ENABLE_OPT OFF` (`engine/render/shader/CMakeLists.txt:40-45`) |
| SPIR-V on-disk cache | **GEMİDE** (X5) | `cd::shader::CachedCompiler` — FNV-1a key = source ⊕ stage ⊕ lang ⊕ **target** ⊕ entry ⊕ debug; `<key_hex>.spv` + `.meta` sidecar (`CachedCompiler.hpp:7-26`) |
| SPIR-V → HLSL | **GEMİDE** (B-infra1) | `cd::spirv_cross_glue::translate()` `Target::kHlsl`, version = SM×10 (`engine/render/spirv_cross_glue/include/cd/spirv_cross_glue/Translate.hpp:32,55`); vcpkg-first + FetchContent `vulkan-sdk-1.3.268.0` fallback (`CMakeLists.txt:40-58`); DAG konumu yalnız `cd::core` (`CMakeLists.txt:18-19`) |
| HLSL → DXIL | **GEMİDE** (Phase 15.C) | `cd::rhi::d3d12::compile_hlsl()` — FXC `D3DCompile2` SM5.1 + DXC `IDxcCompiler3` SM6.0/6.5; RT stage'leri `lib_6_5`, mesh/task `ms_6_5`/`as_6_5` (`engine/render/rhi/src/d3d12/D3D12ShaderCompile.cpp:55-77,154-242`) |
| DXIL tüketimi | **GEMİDE** | `D3D12Device::create_shader_module` ham bytecode blob saklar — format-agnostik (`D3D12Device.cpp:851-868`); DXR pipeline `D3D12_DXIL_LIBRARY_DESC` ile DXIL bekler (`D3D12Device.cpp:1629-1672`) |
| **Kompozisyon (GLSL→DXIL uçtan uca)** | **YOK** — bu ADR'nin kapattığı boşluk | `compile_hlsl` çağıranlar yalnız elle yazılmış HLSL veriyor (`Grep compile_hlsl` → 2 sample) |

### 1.3 Shader korpusu gerçekleri

- Kaynak dili **GLSL'dir ve Vulkan-spesifik uzantılar aktif kullanımda**:
  `GL_EXT_ray_query` (inline RT — chrome yansıma/W8-BE), `GL_EXT_nonuniform_qualifier`
  (bindless doku seti), mesh-shader stage'leri glslang `EShLangMesh/Task` ile
  derleniyor (`Compiler.hpp:83-88`). Yalnız `hello_engine` içinde 54
  `layout(location=...)` arabirim bloğu + 6 on-disk `.glsl` + ~990 satır inline
  GLSL literal var (ROADMAP §3.1).
- ADR-20260608-X5 §2.1 **kilitli karar**: glslang tek in-process toolchain'dir;
  HLSL-master'a geçiş Vulkan tarafında reddedilmiştir (glslang HLSL frontend'i
  RT + mesh-shader yolu için eksik). X4-A bu kararı miras alır.
- Metal yolu (ADR-20260530 §1.5) aynı `spirv_cross_glue` kütüphanesinden MSL
  üretir. D3D12 için HLSL hedefi eklemek **sıfır yeni vendor** demektir.

### 1.4 SOTA karşılaştırması

> İşaret: aşağıdaki satırlar bilgi-tabanından (cutoff 2026-01); çevrimiçi
> doğrulama yapılmadı. Spekülatif kısımlar `[?]` ile işaretli.

| Motor | Kaynak dili | D3D12 yolu | Bizim için ders |
| --- | --- | --- | --- |
| **bgfx** | Kendi GLSL-lehçesi DSL | Offline `shaderc` → backend-başına binary (D3D için FXC-çağı profiller ağırlıklı `[?]`) | Tek-kaynak + offline derleme modeli doğru; ama özel DSL bakım yükü bizim kapsamımız dışında |
| **Diligent Engine** | HLSL master (GLSL de kabul) | Vulkan'a glslang/DXC ile, Metal/GLES'e SPIRV-Cross ile her yöne | "Her yöne çevir" matrisi güçlü ama HLSL-master X5 §2.1 ile çelişir |
| **The Forge** | FSL (kendi DSL) | FSL codegen → API-başına HLSL/GLSL/MSL | Özel DSL = ek derleyici bakımı; reddederiz |
| **Unreal** | HLSL master | Konsol/PC'de DXC; Vulkan/Metal için ShaderConductor mirası = DXC SPIR-V backend + SPIRV-Cross `[?]` | SPIRV-Cross prodüksiyonda kanıtlı (Unreal'ın Metal/Vulkan çapraz yolu) |
| **Dawn/WebGPU** | WGSL | Tint IR → HLSL writer | Tint WGSL-merkezli; bizim uzantı zarfımızı (ray query, mesh) taşımaz |
| **Godot 4.3+** | GLSL | SPIR-V → Mesa NIR `spirv_to_dxil` `[?]` | Tek-kaynak GLSL + SPIR-V→DXIL emsali VAR; ama Mesa NIR vendoring maliyeti bizim Tier-B disiplinimizle bağdaşmaz |

Ortak desen: **hiçbir SOTA motor backend başına shader korpusu çiftlemez**.
Hepsi tek kaynak + çeviri katmanı kullanır. Ayrım yalnız çeviri katmanının
seçiminde (SPIRV-Cross / Tint / NIR / özel DSL).

---

## 2. Karar (Decision)

### 2.1 Seçilen yol — Seçenek A genişletilmiş: tek-kaynak GLSL, SPIRV-Cross + DXC zinciri

**GLSL tek kaynak kalır. D3D12 shader yolu şudur:**

```text
GLSL (tek kaynak, Vulkan-stili)
  │  cd::shader::GlslangCompiler            (gemide)
  ▼
SPIR-V (vulkan1.3 semantiği)
  │  cd::spirv_cross_glue::translate(kHlsl, 65)   (gemide)
  ▼
HLSL SM 6.x kaynak metni
  │  cd::rhi::d3d12::compile_hlsl{ kSM6_5, DXC }  (gemide)
  ▼
DXIL container  →  IDevice::create_shader_module (D3D12)
```

Not: SPIRV-Cross **doğrudan DXBC/DXIL üretmez** — HLSL kaynak metni üretir;
DXC zincirin zorunlu arka yarısıdır. Bu yüzden karar "SPIRV-Cross **veya**
DXC" değil, "SPIRV-Cross **artı** DXC"dir. İki bileşen de ADR-016'da K1
(asla replace edilmez) kategorisindedir ve **ikisi de zaten ağaçtadır**.

Karar bileşenleri:

1. **Shader Model tabanı = SM 6.5 (DXC).** `target_for_sm6` zaten RT için
   `lib_6_5`, mesh için `ms_6_5/as_6_5` standardize etmiş durumda. FXC/SM5.1
   yolu (`compile_sm5_d3dcompile`) yalnız mevcut basit sample'ların legacy
   fallback'i olarak kalır; **yeni hiçbir yol SM5.1 hedeflemez** (RT + bindless
   + mesh SM6 ister).
2. **Kompozisyon noktası `cd::rhi::d3d12` katmanıdır** (yeni başlık
   `D3D12ShaderToolchain.hpp`, §2.4). Gerekçe — DAG temizliği:
   `Compiler.hpp:64-67` kuralı "rhi shader'ı kullanabilir, tersi olamaz";
   `spirv_cross_glue` yalnız `cd::core`'a bağlı. Kompozisyonu `cd::shader`'a
   koymak shader→rhi (dxcapi) bağımlılığı doğururdu = layer violation.
3. **Release dağıtım modeli X5 §2.1 ile simetrik**: build-time
   `cd_compile_glsl_to_dxil(...)` CMake fonksiyonu (X5'in
   `cd_compile_glsl_to_spv` ikizi) `.dxil` sidecar üretir; release D3D12
   binary'si runtime'da NE glslang NE SPIRV-Cross NE DXC ister — yalnız blob
   loader. Debug/dev döngüsü runtime zincirle hot-reload'a açık kalır.
4. **Vendoring planı (ADR-016 uyumlu, sıfır yeni bağımlılık):**
   - SPIRV-Cross: mevcut vcpkg-first + Tier-B FetchContent
     (`vulkan-sdk-1.3.268.0`) düzeni AYNEN kalır. X4-C'nin "Tier-B FetchContent
     investigation" maddesi bu ADR ile **büyük ölçüde kapanmıştır** — kütüphane
     + test zaten gemide; kalan iş kompozisyon + cache.
   - DXC: **FetchContent ile DERLENMEZ** (LLVM-ölçekli build; reddedilir).
     Mevcut desen korunur: `dxcapi.h` + runtime `DxcCreateInstance`
     (`dxcompiler.dll`); DLL yoksa `kDxcUnavailable` zarif hatası
     (`D3D12ShaderCompile.cpp:166-179`). Tekrarlanabilirlik için build-devops
     X4-H'de pinli bir DXC release binary'sini (GitHub `microsoft/DirectXShaderCompiler`
     release zip) build çıktısının yanına kopyalamayı DEĞERLENDİREBİLİR —
     K1 "official binary" politikasına uygundur; zorunlu değildir.
   - glslang: değişiklik yok (X5 §2.1 zaten kilitledi).

### 2.2 `TargetEnv` genişletmesi (CachedCompiler cache-key etkisi)

`cd::shader::TargetEnv`'e **tek değer** eklenir (append-only):

```cpp
/// Hedef bytecode ortamı. (Doc yorumu güncellenir: artık yalnız
/// "Vulkan dialect" değil — emisyon hedef ortamı.)
enum class TargetEnv : std::uint8_t
{
    kVulkan12,
    kVulkan13,
    kD3d12Sm65,  ///< X4-A: glslang(vulkan1.3 SPIR-V) → SPIRV-Cross HLSL(SM6.5)
                 ///<       → DXC → DXIL container. Cache sidecar: <key>.dxil
};
```

Cache-key etkileri (CachedCompiler.hpp:7-26 sözleşmesine göre):

- **Şema değişikliği YOK**: `target` zaten FNV-1a girdisi. `kD3d12Sm65`
  blob'ları `kVulkan13` `.spv` blob'larından doğal olarak ayrı hash'lenir.
- **Append-only güvencesi**: enum değeri sona eklendiği için `kVulkan12/13`'ün
  sayısal değerleri değişmez → mevcut `.spv` cache'leri GEÇERLİ kalır,
  toplu invalidation olmaz.
- **Sidecar uzantısı**: `kD3d12Sm65` hedefinde dosya `<key_hex>.dxil` yazılır
  (`.spv` değil); `.meta` satırı target token'ı içerir (cache-bust aracı
  hedef-başına temizlik yapabilsin).
- **Toolchain-epoch tuzu (ZORUNLU)**: D3D12 hedefi iki ek araca bağımlıdır
  (SPIRV-Cross tag'i + runtime DXC sürümü). Vendor tag bump'ı sonrası bayat
  DXIL dönmemesi için yalnız `kD3d12Sm65` yolunda hash'e
  `inline constexpr std::uint32_t kD3d12ToolchainEpoch = 1;` katılır;
  SPIRV-Cross tag'i veya minimum DXC sürümü değişince epoch elle bump edilir.
  (Vulkan yolunun key'ine DOKUNULMAZ — mevcut cache'ler korunur.)

`ICompiler::compile`'ın dönüş tipi (`CompileResult::spirv`,
`std::vector<std::uint32_t>`) Vulkan hedefleri için değişmez. `kD3d12Sm65`
cache'i `CachedCompiler`'ın `CacheKey::compute` disiplinini **paylaşır** ama
DXIL üretimi `ICompiler` arabiriminin arkasına saklanmaz — kompozisyon §2.4'teki
ayrı rhi-katmanı fonksiyonudur. (Gerekçe: `CompileResult`'a `std::byte` blob +
discriminator eklemek her Vulkan çağıranını sızdıran bir union'a zorlardı;
magic-type/God-interface vetosu.)

### 2.3 Hot-reload + X5 entegrasyonu

X5'in `FileWatcher`/`HotReloadBus` tasarımı kaynak-dosya odaklıdır ve dil
bilmez; D3D12 backend'inde hot-reload aynı `.glsl` dosyasını izler, recreate
yolunda §2.1 zinciri çalışır. X5 ADR'sindeki "path resolver hangi uzantı
gelirse kabul etmeli" şartı (ROADMAP §6 Q1 notu) bu kararla sabitlenir:
çözümleme sırası D3D12'de `<source>.dxil` → `<source>.glsl`+runtime zinciri
(Vulkan'daki `<source>.spv` → `<source>.glsl` ile simetrik).

### 2.4 Developer kontratı — X4-C implementer için header yüzeyi

Yeni başlık: `engine/render/rhi/include/cd/rhi/d3d12/D3D12ShaderToolchain.hpp`
(yalnız Windows-dışı platformda `kBackendUnavailable` dönen stub TU ile —
`D3D12ShaderCompile.cpp` deseni):

```cpp
namespace cd::rhi::d3d12
{

/// GLSL → SPIR-V → HLSL(SM6.x) → DXIL uçtan uca zincir tanımı.
struct GlslToDxilDesc
{
    std::string_view      glsl_source;                    ///< Vulkan-stili GLSL. Boş olamaz.
    cd::rhi::ShaderStage  stage { cd::rhi::ShaderStage::kVertex };
    std::string_view      entry_point { "main" };         ///< HLSL çıktısındaki entry; GLSL "main" kalır.
    std::string_view      source_name { "<inline>" };
    ShaderModel           model { ShaderModel::kSM6_5 };  ///< kSM5_1 bu yolda GEÇERSİZ (kInvalidArgument).
    std::uint32_t         optimization_level { 3 };
    bool                  generate_debug_info { false };
};

/// Zinciri çalıştırır. `spirv_compiler` çağıranın sahipliğindedir (X5'in
/// CachedCompiler'ı geçilebilir — SPIR-V yarısı otomatik cache'lenir).
/// Dönen blob doğrudan ShaderModuleDesc::code/code_size'a verilir.
[[nodiscard]] cd::core::Result<std::vector<std::uint8_t>>
compile_glsl_to_dxil(cd::shader::ICompiler& spirv_compiler,
                     const GlslToDxilDesc&  desc);

}  // namespace cd::rhi::d3d12
```

Invariantlar (implementer bağlayıcı):

1. Hata zincirleme: hangi aşama düşerse düşsün (`glslang` / `translate` / `DXC`)
   dönen `ErrorCode` mesajı aşama adıyla öneklenir ("spirv-cross: ...") —
   üç araçlı zincirde kör hata ayıklama yasak.
2. `translate()` çağrısı `Target::kHlsl`, `version = 65` kullanır
   (`Translate.hpp:55` sözleşmesi: SM×10 + minor).
3. Exception sınırı: SPIRV-Cross iç istisnaları `translate()` içinde zaten
   yakalanıyor (`Translate.hpp:14-17`); bu fonksiyondan istisna KAÇMAZ.
4. DXIL cache'i (X4-C2, opsiyonel ikinci adım): `CachedCompiler`'ın key
   disiplini + §2.2 epoch tuzu; `engine/render/shader`'a rhi bağımlılığı
   SOKMADAN — cache sınıfı `cd::shader` tarafında kalır, DXIL baytlarını
   opak blob olarak saklar.
5. Stage eşlemesi `cd::rhi::ShaderStage` → `cd::shader::ShaderStage`
   çevirisini içerir (iki enum bilinçli kopyadır, `Compiler.hpp:64-67`).

### 2.5 Doğrulama spike'ı (X4-C done-criterion'a girer)

İki SPIRV-Cross HLSL backend yeteneği bilgi-tabanından **kesinlenemedi** ve
implementasyondan ÖNCE küçük bir spike testiyle kanıtlanmalıdır:

- **S1 — `OpRayQueryKHR` çevirisi**: `GL_EXT_ray_query` içeren minimal bir
  fragment shader'ın SM6.5 HLSL `RayQuery<>` çıktısına çevrildiği doğrulanır.
  Desteklenmiyorsa: RT'li shader'lar için sınırlı el-yazımı HLSL adası
  (yalnız ray-query bloğu) fallback'i devreye girer — korpusun geri kalanı
  kararı etkilenmez.
- **S2 — mesh/task stage çevirisi**: `GL_EXT_mesh_shader` çıktısının
  `ms_6_5/as_6_5` HLSL'e çevrimi. Desteklenmiyorsa mesh-shader demo'ları
  D3D12'de Phase-sonrası ertelenir (Vulkan yolu etkilenmez).

Tam DXR pipeline stage'leri (raygen/miss/closesthit, `lib_6_5`) için
SPIRV-Cross çevirisi BEKLENMEZ; motorun RT korpusu inline ray query'dir
(W8-BE). X4-E3'ün DXR PSO ihtiyacı doğarsa o shader'lar küçük HLSL adası
olarak yazılır — bu, "tek kaynak" ilkesinin dokümante edilmiş tek istisnasıdır.

---

## 3. Reddedilen alternatifler

### 3.1 Seçenek B1 — HLSL ikiz korpus (her shader iki kez yazılır)

**Red.** ~54 arabirim bloğu + ~990 satır inline GLSL + 6 on-disk dosyanın
ikizlenmesi; her W8/R-serisi shader patch'i iki dilde bakım ister; X5
hot-reload iki watcher + iki recreate yolu gerektirir. Run 18/W8-BE
uzantı yoğun korpusta (ray query + bindless) sessiz davranış sapması riski
en yüksek seçenek. Hiçbir SOTA motorun seçmediği yol (§1.4).

### 3.2 Seçenek B2 — Doğrudan SPIR-V → DXIL (HLSL ara metni olmadan)

**Red.** Bakımlı, bağımsız bir SPIR-V→DXIL aracı yok:

- `dxil-spirv` (vkd3d-proton) **ters yöndedir** (DXIL→SPIR-V; D3D12-on-Vulkan
  için) `[bilgi-tabanı]`.
- Mesa NIR `spirv_to_dxil` doğru yöndedir (Godot 4.3 D3D12 emsali `[?]`) ama
  Mesa ekosistemini vendorlamak Tier-B "lean FetchContent" disipliniyle
  bağdaşmaz (build karmaşıklığı Skia'nın reddedildiği gerekçeyle aynı sınıf).
- DXC'nin kendisi SPIR-V'yi girdi olarak almaz (HLSL→{DXIL,SPIR-V} derleyicisidir).

### 3.3 Seçenek B3 — HLSL-master'a dönüş (ROADMAP §6 Q1 şık c)

**Red.** ADR-20260608-X5 §2.1 bunu Vulkan yolu için zaten reddetti: glslang
HLSL frontend'i RT + mesh-shader yolumuz için eksik; DXC'nin SPIR-V backend'i
ile değiştirmek ise tüm mevcut GLSL korpusunu (%100) geçersiz kılar ve X5'in
kilitlediği release-`.spv` modelini yeniden açar. Geri-dönüş maliyeti en
yüksek seçenek.

### 3.4 Seçenek C — Tint (Dawn) / naga (wgpu)

**Red.** Her ikisi WebGPU özellik zarfına şekillenmiş WGSL-merkezli IR'lardır:
`GL_EXT_ray_query` eşleniği çapraz çeviride taşınmaz, mesh-shader desteği
yok/deneysel `[bilgi-tabanı]`, GLSL girişi birinci-sınıf değil (ek IR sıçraması
gerekir). Motorun ihtiyaç duyduğu uzantı zarfının altında kalırlar.

### 3.5 shaderc vendorlamak / Slang'e şimdi geçmek

**Red (şimdilik).** shaderc = glslang sarmalayıcısı, sıfır işlevsel delta
(X5 §2.1 gerekçesi aynen geçerli). Slang K1 vendor listesinde ve `ICompiler`
arkasında `make_slang_compiler()` stub'ı hazır (`Compiler.hpp:165`) — gelecekte
A/B testi mümkündür; ama bugün geçiş, kanıtlanmış glslang+SPIRV-Cross zinciri
yerine tek-vendor tüm-yol bağımlılığı koyar. Karar değil, opsiyon olarak kalır.

---

## 4. Sonuçlar (Consequences)

### 4.1 Pozitif

- **Sıfır yeni vendor**: zincirin üç halkası da ağaçta (glslang, SPIRV-Cross,
  DXC runtime). X4-C'nin kapsamı "araştırma + kütüphane" → "kompozisyon +
  cache + spike" düzeyine iner; DAG'deki kritik yol kısalır.
- Tek shader korpusu: her W8/R-serisi düzeltme otomatik olarak D3D12'ye akar;
  golden parity testi (X4-G, FLIP ≤ 1e-3) gerçekten aynı shader'ı karşılaştırır.
- Metal (MSL) ile simetri: tek `translate()` yüzeyi, üç hedef — cross-API
  iddiası tek noktadan kanıtlanır.
- Release tüketicisi sıfır toolchain taşır (`.dxil`/`.spv` sidecar modeli).
- Cache şeması kırılmaz: `TargetEnv` append-only + epoch tuzu yalnız yeni yolda.

### 4.2 Negatif / Risk

- Üç araçlı zincir = üç hata yüzeyi. Mitigasyon: §2.4 invariant 1 (aşama
  önekli hatalar) + her aşamanın ayrı gtest'i.
- SPIRV-Cross HLSL backend'inin ray-query/mesh kapsamı doğrulanmadı —
  §2.5 spike'ları X4-C done-criterion'dur; S1 düşerse sınırlı HLSL adası
  istisnası devreye girer (dokümante edilmiş, korpus geneline yayılmaz).
- Üretilen HLSL insan-okur ama insan-yazımı değil — D3D12 RenderDoc/PIX
  oturumlarında shader debug konforu el yazımı HLSL'den düşük.
  `generate_debug_info` + DXC `-Zi` kısmen telafi eder.
- DXC runtime DLL bağımlılığı (Windows SDK): CI lane'inde (X4-H, NVIDIA
  self-hosted) `dxcompiler.dll` varlığı smoke-check'e eklenmelidir.

### 4.3 Etkilenen modüller

| Modül | Etki |
| --- | --- |
| `engine/render/shader/include/cd/shader/Compiler.hpp` | `TargetEnv::kD3d12Sm65` + doc yorumu güncellemesi (Edit) |
| `engine/render/shader/include/cd/shader/CachedCompiler.hpp` | `.dxil` sidecar + epoch tuzu dokümantasyonu (Edit) |
| `engine/render/rhi/include/cd/rhi/d3d12/D3D12ShaderToolchain.hpp` | YENİ başlık (§2.4 kontratı) |
| `engine/render/spirv_cross_glue/` | Değişiklik yok (tüketici eklenir) |
| `samples/render/hello_d3d12_pbr/` | M4G-V2: inline Blinn-Phong HLSL → gerçek PBR GLSL zinciri (X4-F) |
| CMake | `cd_compile_glsl_to_dxil(...)` helper (X5'in `cd_compile_glsl_to_spv` ikizi) |
| docs/ROADMAP_PHASE_2.md | §6 Q1 KAPANIR; X4-C kapsam notu güncellenir |

### 4.4 Test kapısı

- Yeni: `test_d3d12_shader_toolchain.cpp` — (1) üç aşamalı zincir smoke
  (prim.vert.glsl → DXIL container magic doğrulaması), (2) S1/S2 spike
  sonuç kilidi, (3) `kSM5_1` reddi negatif testi, (4) cache hit/miss +
  epoch-bump invalidation testi.
- Mevcut `test_backend_parity.cpp` + `test_translate.cpp` yeşil kalır.
- Golden parity (X4-G) bu zincirin çıktısıyla koşar — zincir, parity testinin
  ön şartıdır.
