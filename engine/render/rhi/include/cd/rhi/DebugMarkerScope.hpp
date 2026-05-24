// =============================================================================
// CHROMODYNAMIC — cd/rhi/DebugMarkerScope.hpp
// Phase 72.A / Wave 240 — RAII wrapper for push_debug_group/pop_debug_group.
//
// `DebugMarkerScope { cb, "ShadowPass" }` calls `cb.push_debug_group()`
// on construction and `cb.pop_debug_group()` on destruction. Eliminates
// "forgot to pop" bugs and makes RenderDoc / PIX / NSight captures
// readable by scope.
//
//   {
//       cd::rhi::DebugMarkerScope _ { cb, "Shadow" };
//       record_shadow_draws(cb);
//   }   // pop happens here
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/rhi/ICommandBuffer.hpp>

#include <string_view>

namespace cd::rhi
{

class DebugMarkerScope
{
public:
    DebugMarkerScope(ICommandBuffer& cb, std::string_view name)
        : cb_ { &cb }
    {
        cb_->push_debug_group(name);
    }

    DebugMarkerScope(const DebugMarkerScope&) = delete;
    DebugMarkerScope& operator=(const DebugMarkerScope&) = delete;

    ~DebugMarkerScope() noexcept
    {
        if (cb_) cb_->pop_debug_group();
    }

private:
    ICommandBuffer* cb_;
};

}  // namespace cd::rhi
