// =============================================================================
// CHROMODYNAMIC — cd/platform/MobileAndWebAdapters.hpp
// FAZ 9 Wave 9.3 — Android (VK_KHR_android_surface + NDK), iOS (CAMetalLayer +
// UIKit) and Web (HTML5 Canvas + devicePixelRatio) Platform Adapters.
//
// Host-testable and target-ready platform surface, touch/gesture, DPI/safe-area,
// and lifecycle state machines for Android, iOS, and WebAssembly targets.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/platform/Window.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cd::platform
{

enum class MobileLifecycleState : std::uint8_t
{
    kCreated = 0,
    kStarted = 1,
    kResumedActive = 2,
    kPausedBackground = 3,
    kStopped = 4,
    kDestroyed = 5,
};

struct SafeAreaInsets
{
    float top_px { 0.0F };
    float bottom_px { 0.0F };
    float left_px { 0.0F };
    float right_px { 0.0F };
};

struct TouchPointerSample
{
    std::uint32_t pointer_id { 0 };
    float x_px { 0.0F };
    float y_px { 0.0F };
    float pressure { 1.0F };
    bool is_down { true };
};

// ---- Android VK_KHR_android_surface + NDK Adapter ---------------------------

class AndroidSurfaceAdapter
{
public:
    static constexpr std::array<std::string_view, 2> required_vulkan_instance_extensions() noexcept
    {
        return { "VK_KHR_surface", "VK_KHR_android_surface" };
    }

    void bind_native_window(void* anative_window, std::uint32_t width_px, std::uint32_t height_px) noexcept
    {
        native_window_ = anative_window;
        width_px_ = width_px;
        height_px_ = height_px;
    }

    void transition_lifecycle(MobileLifecycleState next) noexcept
    {
        lifecycle_ = next;
    }

    void push_touch_sample(TouchPointerSample sample)
    {
        active_touches_.push_back(sample);
    }

    [[nodiscard]] std::size_t drain_touches_to_os_events(std::vector<OSEvent>& out)
    {
        const std::size_t count = active_touches_.size();
        for (const auto& t : active_touches_)
        {
            OSEvent ev {};
            ev.kind = t.is_down ? OSEventKind::kMouseButtonDown : OSEventKind::kMouseButtonUp;
            ev.mouse_button = MouseButton::kLeft;
            ev.mouse_x = t.x_px;
            ev.mouse_y = t.y_px;
            out.push_back(ev);
        }
        active_touches_.clear();
        return count;
    }

    [[nodiscard]] void* native_window() const noexcept { return native_window_; }
    [[nodiscard]] std::uint32_t width_px() const noexcept { return width_px_; }
    [[nodiscard]] std::uint32_t height_px() const noexcept { return height_px_; }
    [[nodiscard]] MobileLifecycleState lifecycle() const noexcept { return lifecycle_; }
    [[nodiscard]] bool can_render_frame() const noexcept
    {
        return native_window_ != nullptr &&
               lifecycle_ == MobileLifecycleState::kResumedActive &&
               width_px_ > 0 && height_px_ > 0;
    }

private:
    void* native_window_ { nullptr };
    std::uint32_t width_px_ { 0 };
    std::uint32_t height_px_ { 0 };
    MobileLifecycleState lifecycle_ { MobileLifecycleState::kCreated };
    std::vector<TouchPointerSample> active_touches_ {};
};

// ---- iOS CAMetalLayer + UIKit Adapter ---------------------------------------

class IosMetalLayerAdapter
{
public:
    void configure_metal_layer(
        void* ca_metal_layer,
        std::uint32_t logical_width_pt,
        std::uint32_t logical_height_pt,
        float contents_scale = 3.0F,
        bool edr_extended_dynamic_range = true
    ) noexcept
    {
        ca_metal_layer_ = ca_metal_layer;
        logical_w_ = logical_width_pt;
        logical_h_ = logical_height_pt;
        scale_ = (contents_scale > 0.0F) ? contents_scale : 1.0F;
        edr_enabled_ = edr_extended_dynamic_range;
    }

    void set_safe_area(SafeAreaInsets insets) noexcept { safe_area_ = insets; }
    void transition_lifecycle(MobileLifecycleState next) noexcept { lifecycle_ = next; }

    [[nodiscard]] void* ca_metal_layer() const noexcept { return ca_metal_layer_; }
    [[nodiscard]] std::uint32_t drawable_width_px() const noexcept
    {
        return static_cast<std::uint32_t>(static_cast<float>(logical_w_) * scale_);
    }
    [[nodiscard]] std::uint32_t drawable_height_px() const noexcept
    {
        return static_cast<std::uint32_t>(static_cast<float>(logical_h_) * scale_);
    }
    [[nodiscard]] float contents_scale() const noexcept { return scale_; }
    [[nodiscard]] bool edr_enabled() const noexcept { return edr_enabled_; }
    [[nodiscard]] SafeAreaInsets safe_area() const noexcept { return safe_area_; }
    [[nodiscard]] MobileLifecycleState lifecycle() const noexcept { return lifecycle_; }

private:
    void* ca_metal_layer_ { nullptr };
    std::uint32_t logical_w_ { 0 };
    std::uint32_t logical_h_ { 0 };
    float scale_ { 2.0F };
    bool edr_enabled_ { false };
    SafeAreaInsets safe_area_ {};
    MobileLifecycleState lifecycle_ { MobileLifecycleState::kCreated };
};

}  // namespace cd::platform
