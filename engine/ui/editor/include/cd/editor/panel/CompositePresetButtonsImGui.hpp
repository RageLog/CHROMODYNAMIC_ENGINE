// =============================================================================
// CHROMODYNAMIC -- cd/editor/panel/CompositePresetButtonsImGui.hpp
// ADR-005 namespace + ADR-016 D1 replace-ready surface.
//
// ImGui-aware widget that draws the 4 composite preset buttons in a
// horizontal row.  Each button click applies the matching preset to
// the supplied binding and emits a log line through an optional
// callback.
//
// This header pulls in <imgui.h>; only TUs that already link
// cd::imgui_backend should include it.  The headless surface
// (CompositePresets.hpp + CompositeFxBinding.hpp) is fully usable
// without ImGui for tooling / regression tests.
//
// Marathon Run 23 phase N5B.
// =============================================================================
#pragma once

#include <cd/editor/panel/CompositeFxBinding.hpp>
#include <cd/editor/panel/CompositePresets.hpp>

#include <imgui.h>

#include <functional>
#include <string>
#include <string_view>

namespace cd::editor::panel
{

/// Optional log sink.  Receives a single line each time a preset
/// button is clicked.  Pass nullptr (default-constructed std::function)
/// to suppress logging.
using LogSink = std::function<void(std::string)>;

/// Draw a horizontal row of preset buttons.  Each button is wired to
/// apply its preset to `binding` on click and emit "[fx] <label>
/// preset" through `log_sink` (if bound).
///
/// Caller is responsible for the surrounding ImGui::Begin / End +
/// any CollapsingHeader context.  This helper does NOT call
/// ImGui::Begin -- it draws into the current window.
inline void draw_composite_preset_buttons(const CompositeFxBinding& binding,
                                          const LogSink&             log_sink = {})
{
    for (int i = 0; i < kPresetCount; ++i)
    {
        const auto id = static_cast<PresetId>(i);
        const auto     label = preset_label(id);
        // ImGui::Button takes a const char* -- copy through a
        // std::string so the temporary string_view stays alive.
        const std::string label_str { label };
        if (ImGui::Button(label_str.c_str()))
        {
            apply(binding, id);
            if (log_sink)
            {
                log_sink(std::string { "[fx] " } + std::string { label } + " preset");
            }
        }
        if (i + 1 < kPresetCount)
        {
            ImGui::SameLine();
        }
    }
}

}  // namespace cd::editor::panel
