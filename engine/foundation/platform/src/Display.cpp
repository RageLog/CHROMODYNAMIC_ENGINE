// =============================================================================
// CHROMODYNAMIC — cd/platform/Display.cpp
// Win32 monitor enumeration via EnumDisplayMonitors / GetMonitorInfoW /
// EnumDisplaySettingsW + GetDpiForMonitor (SetProcessDpiAwarenessContext).
// Non-Win32 hosts get empty stubs at the bottom of this file.
// =============================================================================
#include <cd/platform/Display.hpp>

#if defined(_WIN32)

    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
    // ShellScalingApi.h for GetDpiForMonitor — requires linking Shcore.lib.
    #include <shellscalingapi.h>

    #include <algorithm>
    #include <optional>
    #include <vector>

namespace cd::platform
{

namespace
{

struct EnumCtx
{
    std::vector<DisplayInfo> result;
    std::uint32_t idx { 0 };
};

BOOL CALLBACK monitor_enum_proc(HMONITOR hmon, HDC /*hdc*/, LPRECT /*rect*/, LPARAM lparam) noexcept
{
    auto* ctx = reinterpret_cast<EnumCtx*>(lparam);

    MONITORINFOEXW mi {};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(hmon, &mi))
        return TRUE;  // skip this monitor but continue enumeration

    DisplayInfo di;
    di.index      = ctx->idx++;
    di.is_primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0U;

    // Work area (excludes taskbar).
    di.work_x = static_cast<std::int32_t>(mi.rcWork.left);
    di.work_y = static_cast<std::int32_t>(mi.rcWork.top);
    di.work_w = static_cast<std::uint32_t>(mi.rcWork.right  - mi.rcWork.left);
    di.work_h = static_cast<std::uint32_t>(mi.rcWork.bottom - mi.rcWork.top);

    // Resolution + refresh from EnumDisplaySettingsW.
    DEVMODEW dm {};
    dm.dmSize = sizeof(dm);
    if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) != 0)
    {
        di.width_px    = dm.dmPelsWidth;
        di.height_px   = dm.dmPelsHeight;
        // MSDN: dmDisplayFrequency == 0 or 1 means "hardware default" (not a real
        // Hz value).  Normalise both to 0 ("unknown") so callers and plausibility
        // tests (refresh_hz >= 24) are never confused by the sentinel.
        di.refresh_hz  = (dm.dmDisplayFrequency <= 1U) ? 0U : dm.dmDisplayFrequency;
    }
    else
    {
        // Fallback: use monitor rect (virtual-screen coordinates, DPI-unscaled).
        di.width_px  = static_cast<std::uint32_t>(mi.rcMonitor.right  - mi.rcMonitor.left);
        di.height_px = static_cast<std::uint32_t>(mi.rcMonitor.bottom - mi.rcMonitor.top);
    }

    // DPI scale via GetDpiForMonitor (requires PROCESS_PER_MONITOR_DPI_AWARE
    // or equivalent; falls back to 96 DPI if the call fails).
    UINT dpi_x = 96U;
    UINT dpi_y = 96U;
    // GetDpiForMonitor is available from Windows 8.1 (Shcore.dll).
    if (GetDpiForMonitor(hmon, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y) != S_OK)
    {
        dpi_x = 96U;
    }
    di.dpi_scale = static_cast<float>(dpi_x) / 96.0F;

    ctx->result.push_back(di);
    return TRUE;
}

}  // namespace

std::vector<DisplayInfo> enumerate_displays() noexcept
{
    EnumCtx ctx;
    EnumDisplayMonitors(nullptr, nullptr, &monitor_enum_proc, reinterpret_cast<LPARAM>(&ctx));
    return std::move(ctx.result);
}

std::optional<DisplayInfo> primary_display() noexcept
{
    const auto displays = enumerate_displays();
    if (displays.empty())
        return std::nullopt;
    for (const auto& d : displays)
    {
        if (d.is_primary)
            return d;
    }
    return displays.front();
}

}  // namespace cd::platform

#else  // !_WIN32

namespace cd::platform
{

std::vector<DisplayInfo> enumerate_displays() noexcept
{
    return {};
}

std::optional<DisplayInfo> primary_display() noexcept
{
    return std::nullopt;
}

}  // namespace cd::platform

#endif
