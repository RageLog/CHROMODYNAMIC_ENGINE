// =============================================================================
// cd::post_composite unit tests — Push layout + GLSL source sanity.
// =============================================================================
#include <cd/post_composite/Composite.hpp>

#include <cstring>
#include <string_view>

namespace
{

[[nodiscard]] bool push_layout_is_256_bytes() noexcept
{
    return sizeof(cd::post_composite::Push) == 256U;
}

[[nodiscard]] bool binding_count_matches() noexcept
{
    return cd::post_composite::kBindingCount == 6U;
}

[[nodiscard]] bool vs_source_compiles_trivially() noexcept
{
    // Spot-check: VS source declares the fullscreen-triangle UV out
    // varying and uses gl_VertexIndex without extra inputs.
    const std::string_view vs { cd::post_composite::kCompositeVS };
    return vs.find("gl_VertexIndex") != std::string_view::npos &&
           vs.find("v_uv") != std::string_view::npos;
}

[[nodiscard]] bool fs_source_declares_all_bindings() noexcept
{
    const std::string_view fs { cd::post_composite::kCompositeFS };
    return fs.find("cd_hdr_color") != std::string_view::npos &&
           fs.find("cd_bloom_mip0") != std::string_view::npos &&
           fs.find("cd_depth") != std::string_view::npos &&
           fs.find("cd_gbuf_normal") != std::string_view::npos &&
           fs.find("cd_history_prev") != std::string_view::npos &&
           fs.find("cd_gbuf_velocity") != std::string_view::npos;
}

[[nodiscard]] bool fs_writes_dual_mrt() noexcept
{
    const std::string_view fs { cd::post_composite::kCompositeFS };
    return fs.find("out_color") != std::string_view::npos &&
           fs.find("out_history") != std::string_view::npos;
}

}  // namespace

int main()
{
    if (!push_layout_is_256_bytes())          return 1;
    if (!binding_count_matches())              return 2;
    if (!vs_source_compiles_trivially())       return 3;
    if (!fs_source_declares_all_bindings())    return 4;
    if (!fs_writes_dual_mrt())                 return 5;
    return 0;
}
