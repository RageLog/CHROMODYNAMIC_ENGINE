// =============================================================================
// HelloShaderWatch.hpp
// -----------------------------------------------------------------------------
// Per-material on-disk shader hot-reload harness for hello_engine (X5 / M1).
//
// Owns one cd::shader::FileWatcher plus a small registry mapping each watched
// path to its (Material*, MaterialDesc, "what to recreate") closure. Per
// ADR-20260529-X5 we deliberately use the polling watcher; there is no
// background thread and no native event API.
//
// Usage pattern in main.cpp:
//
//   cd_sample::HelloShaderWatch watch;
//   watch.register_material(
//       materials.prim,
//       prim_desc,
//       "samples/engine/hello_engine/shaders/prim.vert.glsl",
//       "samples/engine/hello_engine/shaders/prim.frag.glsl");
//
//   while (true)
//   {
//       window.pump_events(...);
//       watch.poll_and_reload(device, compiler.get());
//       ... draw the frame ...
//   }
//
// Recreate failures are non-fatal: the old pipeline keeps rendering and an
// error message is logged via the supplied logger (fprintf to stderr by
// default). This matches the ADR's "never crash the editor on a typo".
//
// Lifetime: the registered Material& must outlive the HelloShaderWatch. In
// hello_engine this is trivially true — both live in the same main() frame.
// =============================================================================
#pragma once

#include "HelloMaterials.hpp"

#include <cd/material/Material.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/shader/Compiler.hpp>
#include <cd/shader/FileWatcher.hpp>

#include <cstdio>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cd_sample {

/// Per-material hot-reload entry. The recreate closure captures whatever
/// the call-site needs (a copy of the original MaterialDesc, references
/// to the device + compiler, etc.) and is invoked when any of this
/// entry's paths report dirty in the next poll cycle.
struct ShaderWatchEntry
{
    cd::material::Material* material { nullptr };
    std::vector<std::string> paths;
    std::function<bool(cd::rhi::IDevice&, cd::shader::ICompiler*)> recreate;
    std::string name;
};

class HelloShaderWatch
{
public:
    HelloShaderWatch() noexcept = default;

    /// Register a material whose shaders should be re-compiled when any of
    /// `paths` change on disk. The `recreate` callback must return true on
    /// success; on failure it should leave `*material` untouched so the
    /// previous pipeline keeps rendering.
    void add_entry(cd::material::Material* material,
                   std::vector<std::string> paths,
                   std::function<bool(cd::rhi::IDevice&, cd::shader::ICompiler*)> recreate,
                   std::string name)
    {
        for (const auto& p : paths)
            watcher_.add(p);
        entries_.push_back(ShaderWatchEntry { material, std::move(paths), std::move(recreate), std::move(name) });
    }

    /// Convenience overload for the common single-material case: register
    /// a Material* alongside its vertex + fragment source files and a
    /// MaterialDesc factory closure. The factory should populate
    /// `vertex_glsl_path` / `fragment_glsl_path` and any other pipeline
    /// state then return the freshly built Material via
    /// `Material::create`. Used by hello_engine for the prim material.
    template <typename DescFactory>
    void register_material(cd::material::Material& material,
                           std::string_view vertex_glsl_path,
                           std::string_view fragment_glsl_path,
                           std::string name,
                           DescFactory&& make_desc)
    {
        std::vector<std::string> paths { std::string { vertex_glsl_path },
                                         std::string { fragment_glsl_path } };
        auto recreate = [&material, factory = std::forward<DescFactory>(make_desc), entry_name = name](
                            cd::rhi::IDevice& device, cd::shader::ICompiler* compiler) -> bool {
            auto desc = factory();
            auto r = cd::material::Material::create(device, compiler, desc);
            if (!r.has_value())
            {
                std::fprintf(stderr,
                             "[shader-watch] %s recreate FAILED: %.*s (keeping previous pipeline)\n",
                             entry_name.c_str(),
                             static_cast<int>(r.error().message.size()),
                             r.error().message.data());
                return false;
            }
            material = std::move(*r);
            std::fprintf(stderr, "[shader-watch] %s recreate OK\n", entry_name.c_str());
            return true;
        };
        add_entry(&material, std::move(paths), std::move(recreate), std::move(name));
    }

    /// Poll the watcher; on dirty paths, invoke each affected entry's
    /// `recreate`. Call once per frame after Present (so the old pipeline
    /// finishes the in-flight frame). On a dirty cycle this drains the
    /// device (`wait_idle`) BEFORE any pipeline swap per the X5-2 deferred-
    /// release invariant (ADR-20260608 addendum A.2). Returns the number of
    /// entries that were successfully recreated.
    int poll_and_reload(cd::rhi::IDevice& device, cd::shader::ICompiler* compiler)
    {
        if (!watcher_.poll())
            return 0;
        const auto& dirty = watcher_.dirty();

        // X5-2 (ADR-20260608 addendum A.2) — deferred-release / GPU-lifetime
        // guard lives HERE in the reload path, NOT in Material::operator=.
        // A dirty edit means at least one live pipeline is about to be
        // destroyed by the recreate closure's move-assign. Draining the
        // device first upholds the invariant: an old PSO is NEVER destroyed
        // while the GPU may still reference it (Vulkan PSO destroy mid-flight
        // = TDR / device-lost). This is the MVP guard (~1 ms once per edit);
        // a future non-blocking variant routes through cd::rhi::DeferredDestroy
        // (frame-fence-keyed retirement) — OUT OF X5 MVP scope. The drain is
        // done once per dirty poll-cycle, before ANY swap, so a multi-file
        // "save all" amortises a single idle-drain across all swapped
        // pipelines.
        device.wait_idle();

        int recreated = 0;
        for (auto& entry : entries_)
        {
            bool entry_dirty = false;
            for (const auto& p : entry.paths)
            {
                for (const auto& d : dirty)
                {
                    if (p == d)
                    {
                        entry_dirty = true;
                        break;
                    }
                }
                if (entry_dirty)
                    break;
            }
            if (!entry_dirty)
                continue;
            if (entry.recreate(device, compiler))
                ++recreated;
        }
        return recreated;
    }

    [[nodiscard]] std::size_t watched_count() const noexcept
    {
        return watcher_.watched_count();
    }

private:
    cd::shader::FileWatcher watcher_;
    std::vector<ShaderWatchEntry> entries_;
};

}  // namespace cd_sample
