# ADR-20260523 — Wave 97: Mobile RHI architecture (Metal + D3D12)

## Bağlam

Phase 8 routing item #3 — Mobile RHI (Metal / D3D12). Phase 9'a tam
implementation kalsa da, mimari kararı + skeleton header'ları
şimdi indirmek, downstream sample/sandbox kodunun platform-agnostic
yazılmasını sağlar.

Mevcut engine durumu:
- cd::rhi (IDevice / IComputePipeline / IBuffer / IDescriptorSet)
- cd::rhi_vulkan (tek concrete backend)
- CD_ENABLE_VULKAN CMake option (Linux/Windows için ON, macOS/iOS
  için doğal olarak OFF — MoltenVK alternatifi var ama native Metal
  daha verimli).

Phase 8'in geri kalan iki item'ı:
- Vulkan compute pipeline wiring (item #1.b) — Sprint 12 lokal GPU
  test impossible; Phase 9'a kaldı.
- Mobile RHI (item #3) — bu wave kapatıyor.

## Karar

**İki yeni concrete backend factory eklensin, hepsi stub:**

1. `cd::rhi_metal` — macOS / iOS native Metal backend.
   - Factory: `create_metal_device(MetalCreateInfo)` → Result<IDevice>.
   - `__APPLE__` gated. Non-Apple platformlarda factory nullptr döner.
   - Phase 9 implementation: MTLDevice + MTLCommandQueue + MTLLibrary
     for compute. Vertex/fragment pipeline Phase 9+.

2. `cd::rhi_d3d12` — Windows D3D12 native backend.
   - Factory: `create_d3d12_device(D3D12CreateInfo)` → Result<IDevice>.
   - `_WIN32` gated. Non-Windows platformlarda factory nullptr döner.
   - Phase 9 implementation: D3D12Device + CommandQueue + Root
     Signature. Same IDevice contract as Vulkan.

3. `cd::rhi::create_native_device()` — birleşik dispatch factory.
   - Windows → tries D3D12 first, falls back to Vulkan, then null.
   - macOS / iOS → tries Metal first, falls back to Vulkan
     (MoltenVK), then null.
   - Linux → Vulkan only.
   - Pattern mirrors `cd::audio::make_native_audio_backend()` from
     Wave 34.

Common contract: All three backends share `cd::rhi::IDevice`.
Application code calls `create_native_device()` and never knows
which API it got. Per-frame: build commands, submit, wait. No
backend-specific code at the call site.

## Reddedilen alternatifler

- **MoltenVK her macOS'ta:** MoltenVK iOS'ta ücretsiz değil; performans
  native Metal'in altında. Native Metal ileri taşıma maliyetine değer.
- **WebGPU / wgpu olarak unified:** Implementation maturity hâlâ
  beta; native arka uçlardan daha yavaş. Phase 9+ candidate olarak
  saklanıyor.
- **Sprint 12'de tam Metal impl:** macOS runner yok; build + runtime
  validation imkansız. Skeleton + ADR pragmatik karar.
- **`cd::rhi::Backend` enum tag:** Concrete factory'ler explicit;
  dispatch logic linear `#if defined(__APPLE__)`. Tag tabanlı
  dynamic dispatch ek overhead, gerekli değil.

## Sonuçlar

Sprint 12 Wave 97'de skeleton header'lar + factory stub'lar iniyor.
Phase 9 Sprint 1'in açılış item'ı = Metal compute pipeline (mobile
RHI'nin minimum viable kısmı). D3D12 takip eden sprint'lerde.

Sprint 12 v0.17.0 closure (Wave 98) ile ADR + skeleton'lar tag
ediliyor; Phase 9 başlangıç roadmap'i closure ADR'sinde yer alacak.

## Açık sorular

- **MoltenVK vs. native Metal balance:** MoltenVK iOS shipping
  için cazip; native Metal performans için cazip. İlk implementation
  hangisi? Karar: native Metal (uzun vadeli avantaj), MoltenVK
  fallback v0.18'de eklenebilir.
- **D3D12 timing:** Windows zaten Vulkan ile çalışıyor; D3D12 ek
  performans için (HLSL shader cache, NVAPI/AMD extensions). v0.18+
  candidate.
- **Mobile RHI shader compilation:** GLSL → Metal (MoltenVK SPIRV-Cross
  veya doğrudan MSL); GLSL → DXIL (DXC). cd::shader::Compiler'a backend
  parameter olarak eklenecek.
