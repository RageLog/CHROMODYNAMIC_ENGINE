// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/core/VfsAssetPickerModal.hpp
// Standalone Concrete C++23 Construct Class #6: `cd::scene::VfsAssetPickerModal`
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

// [6] FileDialog -> VfsAssetPickerModal (38:09)
class VfsAssetPickerModal final
    : public ChromaConstructBase<6, ConstructCategory::CoreSystem, ConstructRole::Element>
{
public:
    std::string root_mount_point  { "res://" };
    bool        live_mesh_preview { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "VfsAssetPickerModal"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "FileDialog"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "38:09"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "VfsAssetPickerModal + AsyncDirScanner"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<VfsAssetPickerModal>(*this); }
};

} // namespace cd::scene
