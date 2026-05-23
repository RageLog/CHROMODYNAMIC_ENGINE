// =============================================================================
// CHROMODYNAMIC — cd/imgui/ProfilerView.hpp
// Phase 16.D / Wave 174 — header-only ImGui flame-graph widget for
// cd::profile::Sample streams.
//
// Renders a Tracy-style flame graph inside an ImGui window:
//   per-thread lane along Y, time along X, sample colored by name-hash.
//   Hover tooltip with sample name + duration.
//
// Usage:
//   #include <cd/imgui/ProfilerView.hpp>
//   ...
//   const auto samples = sink.snapshot();
//   cd::imgui::profiler_flamegraph(samples);  // call inside an
//                                              // ImGui::Begin block
//
// Header-only. Depends on imgui.h + cd::profile::Sample.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/profile/Scope.hpp>

#include <imgui.h>

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace cd::imgui
{

namespace detail
{

[[nodiscard]] inline ImU32 color_from_name_hash(std::string_view name) noexcept
{
    std::uint32_t h = 2166136261u;
    for (char c : name) { h ^= static_cast<std::uint8_t>(c); h *= 16777619u; }
    // HSV-like shuffle to keep adjacent hashes distinct.
    const std::uint8_t r = static_cast<std::uint8_t>(60 + ((h >>  0) & 0x7F));
    const std::uint8_t g = static_cast<std::uint8_t>(60 + ((h >>  8) & 0x7F));
    const std::uint8_t b = static_cast<std::uint8_t>(60 + ((h >> 16) & 0x7F));
    return IM_COL32(r, g, b, 220);
}

}  // namespace detail

/// Draw a flame-graph view of `samples` inside the current ImGui
/// window. Caller is responsible for wrapping with Begin/End. Pass
/// a contiguous time range to focus on (defaults to "all samples").
inline void profiler_flamegraph(std::span<const cd::profile::Sample> samples,
                                float row_height = 18.0F)
{
    if (samples.empty())
    {
        ImGui::TextDisabled("(no samples in this snapshot)");
        return;
    }

    // Build the time window from the samples themselves.
    std::uint64_t t_min = UINT64_MAX, t_max = 0;
    for (const auto& s : samples)
    {
        if (s.start_ns < t_min) t_min = s.start_ns;
        const std::uint64_t end_ns = s.start_ns + s.duration_ns;
        if (end_ns > t_max) t_max = end_ns;
    }
    if (t_min >= t_max)
    {
        ImGui::TextDisabled("(degenerate sample range)");
        return;
    }
    const double window_ns = static_cast<double>(t_max - t_min);

    // Group samples by thread_hash → row index.
    std::unordered_map<std::uint64_t, int> thread_row;
    int next_row = 0;
    for (const auto& s : samples)
        if (thread_row.try_emplace(s.thread_hash, next_row).second)
            ++next_row;

    ImGui::Text("samples: %zu   threads: %d   window: %.3f ms",
                samples.size(), next_row, window_ns / 1.0e6);

    const float canvas_w = ImGui::GetContentRegionAvail().x;
    const float canvas_h = std::max(row_height,
                                    static_cast<float>(next_row) * row_height);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Background.
    dl->AddRectFilled(origin,
                      ImVec2 { origin.x + canvas_w, origin.y + canvas_h },
                      IM_COL32(30, 30, 36, 255));

    // Sample bars.
    const cd::profile::Sample* hovered = nullptr;
    const ImVec2 mp = ImGui::GetMousePos();
    for (const auto& s : samples)
    {
        const int row = thread_row[s.thread_hash];
        const double x0 = static_cast<double>(s.start_ns - t_min) / window_ns
                          * static_cast<double>(canvas_w);
        const double x1 = static_cast<double>(s.start_ns + s.duration_ns - t_min)
                          / window_ns * static_cast<double>(canvas_w);
        const float fx0 = origin.x + static_cast<float>(x0);
        const float fx1 = origin.x + std::max(static_cast<float>(x1), static_cast<float>(x0) + 1.0F);
        const float fy0 = origin.y + row * row_height;
        const float fy1 = fy0 + row_height - 2.0F;
        dl->AddRectFilled(ImVec2 { fx0, fy0 }, ImVec2 { fx1, fy1 },
                          detail::color_from_name_hash(s.name));
        if (mp.x >= fx0 && mp.x <= fx1 && mp.y >= fy0 && mp.y <= fy1)
            hovered = &s;
    }

    // Consume the canvas rect so subsequent ImGui calls flow below it.
    ImGui::Dummy(ImVec2 { canvas_w, canvas_h });

    if (hovered != nullptr)
    {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(std::string { hovered->name }.c_str());
        ImGui::Separator();
        ImGui::Text("duration : %.3f µs",
                    static_cast<double>(hovered->duration_ns) / 1000.0);
        ImGui::Text("start    : %.3f ms (from window origin)",
                    static_cast<double>(hovered->start_ns - t_min) / 1.0e6);
        ImGui::Text("thread   : %llu",
                    static_cast<unsigned long long>(hovered->thread_hash));
        ImGui::EndTooltip();
    }
}

}  // namespace cd::imgui
