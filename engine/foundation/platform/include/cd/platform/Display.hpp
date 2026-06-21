// =============================================================================
// CHROMODYNAMIC — cd/platform/Display.hpp
// Cross-platform display / monitor enumeration.
//
// Provides a snapshot of connected monitors at the time of the call.
// The snapshot is NOT updated live; call `enumerate_displays()` again after
// a WM_DISPLAYCHANGE (Win32) or equivalent to refresh.
//
// Types:
//   DisplayInfo — one physical monitor:
//     * `index`         — zero-based ordinal in the enumeration order.
//     * `width_px`      — horizontal resolution in physical pixels.
//     * `height_px`     — vertical resolution in physical pixels.
//     * `refresh_hz`    — refresh rate in Hz (0 if unknown).
//     * `dpi_scale`     — DPI scaling factor relative to 96 DPI baseline
//                         (e.g. 1.5 = 144 DPI / "150%" scale in Windows).
//     * `is_primary`    — true for the system's primary monitor.
//     * `work_x`,
//       `work_y`        — top-left of the work area in virtual-screen coords.
//     * `work_w`,
//       `work_h`        — size of the work area in physical pixels.
//
//   enumerate_displays() — returns a snapshot vector; empty on headless hosts.
//   primary_display()    — returns the primary DisplayInfo, or the first one
//                          found; nullopt when no displays are present.
//
// Platform mapping:
//   * Win32  — EnumDisplayMonitors / GetMonitorInfoW / EnumDisplaySettingsW.
//   * Other  — returns an empty vector (stub, no error; callers must handle).
//
// Dependencies: cd::core only.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>
#include <optional>
#include <vector>

namespace cd::platform
{

/// Snapshot of a single physical monitor.
struct DisplayInfo
{
    std::uint32_t index     { 0 };         ///< Zero-based enumeration index.
    std::uint32_t width_px  { 0 };         ///< Horizontal resolution (pixels).
    std::uint32_t height_px { 0 };         ///< Vertical resolution (pixels).
    std::uint32_t refresh_hz { 0 };        ///< Refresh rate in Hz; 0 if unknown.
    float         dpi_scale { 1.0F };      ///< DPI scale (96 DPI baseline = 1.0).
    bool          is_primary { false };    ///< True for the primary monitor.
    std::int32_t  work_x { 0 };           ///< Work-area top-left X (virtual screen).
    std::int32_t  work_y { 0 };           ///< Work-area top-left Y (virtual screen).
    std::uint32_t work_w { 0 };           ///< Work-area width (pixels).
    std::uint32_t work_h { 0 };           ///< Work-area height (pixels).
};

/// Enumerate all connected monitors.
/// Returns an empty vector on headless hosts or when no monitors are detected.
/// The order is implementation-defined; primary monitor is not guaranteed to
/// be first — use `is_primary` or `primary_display()` to find it.
[[nodiscard]] std::vector<DisplayInfo> enumerate_displays() noexcept;

/// Return the primary display, or the first available display if none is
/// flagged as primary.  Returns std::nullopt when no displays are present.
[[nodiscard]] std::optional<DisplayInfo> primary_display() noexcept;

}  // namespace cd::platform
