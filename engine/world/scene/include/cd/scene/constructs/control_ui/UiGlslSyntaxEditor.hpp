// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiGlslSyntaxEditor.hpp
// Standalone Concrete C++23 Construct Class #199: `cd::scene::UiGlslSyntaxEditor`
// =============================================================================
#pragma once

#include <cd/scene/constructs/IChromaConstruct.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace cd::scene
{

// 199: CodeEdit -> UiGlslSyntaxEditor
class UiGlslSyntaxEditor final
    : public ChromaConstructBase<199, ConstructCategory::ControlUI, ConstructRole::Element>
{
public:
    std::string shader_source_path  { "shaders/prim.frag.glsl" };
    bool        hot_reload_on_save  { true };
    bool        show_line_numbers   { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiGlslSyntaxEditor"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "CodeEdit"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "30:38"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiGlslSyntaxEditor + SpirvHotReload"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiGlslSyntaxEditor>(*this); }
};

} // namespace cd::scene
