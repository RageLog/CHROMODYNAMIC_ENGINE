// =============================================================================
// CHROMODYNAMIC — cd/render/PostProcessChain.hpp
// Phase 50.A / Wave 218 — ordered post-process pass list.
//
// Tone-mapping, bloom, FXAA, color grading, vignette — the renderer
// runs an ordered list of "image-in, image-out" passes after the main
// scene draw. PostProcessChain is the data-only container that owns
// the order; binding individual passes to actual rhi pipelines is the
// renderer's job.
//
// Each pass has:
//   * `name` — for ImGui debug + ADR documentation.
//   * `enabled` — toggle without removing.
//   * `params` — opaque uint32 (caller's choice of meaning; typically
//                an offset into a parameter UBO).
//
// `enabled_view()` returns the subset to render; insertion order is
// preserved.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace cd::render
{

struct PostProcessPass
{
    std::string   name;
    bool          enabled { true };
    std::uint32_t params { 0 };
};

class PostProcessChain
{
public:
    void add(std::string name, std::uint32_t params = 0)
    {
        passes_.push_back(PostProcessPass { std::move(name), true, params });
    }

    bool remove(std::string_view name)
    {
        for (auto it = passes_.begin(); it != passes_.end(); ++it)
        {
            if (it->name == name)
            {
                passes_.erase(it);
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] std::size_t size() const noexcept { return passes_.size(); }

    [[nodiscard]] const std::vector<PostProcessPass>& passes() const noexcept { return passes_; }

    void set_enabled(std::string_view name, bool on) noexcept
    {
        for (auto& p : passes_)
            if (p.name == name) p.enabled = on;
    }

    [[nodiscard]] std::vector<const PostProcessPass*> enabled_view() const
    {
        std::vector<const PostProcessPass*> out;
        out.reserve(passes_.size());
        for (const auto& p : passes_)
            if (p.enabled) out.push_back(&p);
        return out;
    }

    void clear() noexcept { passes_.clear(); }

private:
    std::vector<PostProcessPass> passes_;
};

}  // namespace cd::render
