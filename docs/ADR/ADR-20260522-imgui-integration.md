# ADR-20260522 — ImGui integration (cd::imgui)

## Bağlam

Engine'in editor + debug HUD katmanı için ImGui industry-standard. Mevcut
durum:

* `cd::ui` retained-mode widget tree (CPU-only, GPU rendering yok)
* `cd::editor_ui` PropertyInspector / TransformGizmo / SceneTreeView
  (cd::ui üzerine, hâlâ render yok)
* GPU-side bir IMGui yok → her sample kendi triangle/quad'ını yapıyor,
  debug overlay imkanı yok.

ImGui'yi entegre etmek için iki ayrı kararı toplamak gerekiyor:

1. **RHI raw-handle erişimi:** ImGui'nin `imgui_impl_vulkan.cpp` backend'i
   `VkInstance`, `VkPhysicalDevice`, `VkDevice`, `VkQueue`, queue family
   index, descriptor pool, render pass / dynamic-rendering color format,
   ve per-frame `VkCommandBuffer` istiyor. cd::rhi handle-based abstraction
   bunları gizliyor. Nasıl açacağız?

2. **Platform input bridge:** ImGui frame başında ImGuiIO'ya mouse/key
   state vermelidir. cd::platform::OSEvent stream'ini ImGui IO'ya
   çevirmek gerek (Win32 path; Linux/macOS sonra).

## Karar

### (a) RHI raw-handle accessors via free-function downcast

cd::rhi::IDevice ve cd::rhi::ICommandBuffer arayüzlerine VK_* tipleri
sızdırmıyoruz. Bunun yerine `cd::rhi_vulkan` namespace'ine free function
ekliyoruz:

```cpp
namespace cd::rhi_vulkan {
  struct NativeHandles {
    VkInstance instance{VK_NULL_HANDLE};
    VkPhysicalDevice physical{VK_NULL_HANDLE};
    VkDevice device{VK_NULL_HANDLE};
    VkQueue graphics_queue{VK_NULL_HANDLE};
    uint32_t graphics_family{0};
  };
  [[nodiscard]] std::optional<NativeHandles> get_native(cd::rhi::IDevice& dev);

  [[nodiscard]] VkCommandBuffer get_native(cd::rhi::ICommandBuffer& cmd);
}
```

Implementation: `dynamic_cast<VulkanDevice*>(&dev)` döner; null değilse
struct doldurulur. Bu, IDevice abstraction'ını korur — DX12 backend
geldiğinde `cd::rhi_dx12::get_native` ayrı bir free function olur,
karışmaz.

### (b) cd::imgui library structure

Yeni library `engine/imgui_backend/cd::imgui_backend`. FetchContent ile
ImGui v1.91+ (docking branch tercih) çekilir. Wrap edilen:
* `imgui.cpp` + `imgui_demo.cpp` + `imgui_draw.cpp` + `imgui_widgets.cpp`
  + `imgui_tables.cpp` (ImGui core)
* `imgui_impl_vulkan.cpp` (Vulkan backend)
* `imgui_impl_win32.cpp` (Windows input)

Public API (cd::imgui namespace):
```cpp
struct InitDesc {
  cd::platform::Window& window;
  cd::rhi::IDevice& device;
  cd::rhi::Format swapchain_format;
  uint32_t frames_in_flight{2};
};
class Context {
public:
  static cd::core::Result<Context> create(const InitDesc&);
  void new_frame();
  void render(cd::rhi::ICommandBuffer& cmd);
  void handle_event(const cd::platform::OSEvent&);
  // dtor cleans up ImGui + Vulkan resources
};
```

ImGui'nin `imgui_impl_win32_WndProcHandler` raw WM_* alır. cd::platform::
Win32Window mevcut event pump'ını koruyor; biz `handle_event()` ile
OSEvent → ImGuiIO çevirimi yapıyoruz. **Alternatif gelecek için**:
cd::platform'a `set_raw_event_callback()` hook eklenebilir; v1'de OSEvent
yeterli (ImGui basic key + mouse için bu yeterli detay var).

### (c) Render integration

cd::imgui::Context::render() çağrılan ICommandBuffer'a:
1. ImGui::Render() → ImDrawData
2. ImGui_ImplVulkan_RenderDrawData(VkCommandBuffer, ImDrawData)
3. VkCommandBuffer mevcut bir render pass içinde olmalı — sample
   sorumluluğu.

ImGui kendi descriptor pool, font texture, pipeline'larını yönetiyor.
init/shutdown'da bunlar yaratılıyor; bizim engine resource pool'una
girmiyor.

## Reddedilen alternatifler

* **IDevice'a VkInstance vb. virtual accessor eklemek:** Abstraction'ı
  patlatır; DX12 backend bunları null döner ve UB üretir. Mantıklı bir
  cross-API DX12 muadili yok.
* **Kendi immediate-mode UI'ımızı yazmak:** ImGui 20+ yıllık dev
  ergonomy ve community plugin ekosistemi. Boştan yazmak büyük zaman
  kaybı + buggy versiyona ulaşırız.
* **Nuklear:** C library; C++ engine'e idiom uyumsuz. ImGui daha
  popüler, daha güvenilir.
* **RmlUi / Slint:** HTML-CSS-benzeri retained-mode. Editor için aşırı;
  ayrıca dev-tooling primary use case için ImGui norm.

## Sonuçlar

* Editor + debug HUD'lar tek bir Context arkasında.
* hello_imgui sample: clear background + ImGui demo window + cd::profile
  StatsAggregator'ı tablo olarak gösteren custom panel. CI'de
  `--headless 3` ile render-pipeline regression smoke-test edilebilir
  (ImGui draw'lar başarısız olursa output frame buffer farklı olur, ki
  zaten test).
* W10.3 Debug HUD overlay artık ImGui üstüne inşa edilir (mevcut
  cd::profile::StatsAggregator data-side hazır).

## Açık sorular

* Docking branch ve viewports (multi-window editor) v1'de mi v2'de mi?
  Şimdilik docking ON, viewports OFF — viewports cd::platform'a multi-
  window paralleli ister, ayrı sprint.
* ImGui-fonts-bootstrap: default IBM Plex / Roboto FontConfig önerisi —
  v1 ImGui default fontu yeterli, v2 cooked .ttf bundle.
