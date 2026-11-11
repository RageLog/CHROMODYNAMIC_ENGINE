// =============================================================================
// CHROMODYNAMIC STUDIO -- apps/editor/workspace_2d/Workspace2D.cpp
// =============================================================================
#include "workspace_2d/Workspace2D.hpp"

#include "Studio2DWorkspace.hpp"

namespace cd::studio::workspace_2d
{

void draw_workspace_2d(StudioEngineState& s, float dt, std::uint32_t frame_w, std::uint32_t frame_h)
{
    cd::studio::draw_2d_studio_workspace_panels(
        s, dt, frame_w, frame_h,
        [&s](std::string msg) { s.push_log(std::move(msg)); });
}

void record_2d_canvas(StudioEngineState& s,
                      cd::rhi::ICommandBuffer& cmd,
                      cd::rhi::Extent2D extent)
{
    if (!s.ui_submitter || !s.ui_submitter->is_valid())
    {
        return;
    }
    cd::studio::record_2d_canvas_if_active(s.ui_batcher, s.ui_submitter.get(), cmd, extent);
}

}  // namespace cd::studio::workspace_2d
