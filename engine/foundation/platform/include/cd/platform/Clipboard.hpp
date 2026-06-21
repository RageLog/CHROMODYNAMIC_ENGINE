// =============================================================================
// CHROMODYNAMIC — cd/platform/Clipboard.hpp
// Cross-platform UTF-8 clipboard get/set.
//
// Contract:
//   * `get_clipboard_text()` returns the current clipboard contents as a
//     UTF-8 string, or an empty string when the clipboard is empty / contains
//     non-text data.  Never returns an error — a missing clipboard is treated
//     as empty.
//   * `set_clipboard_text(sv)` replaces the clipboard with `sv` encoded as
//     UTF-8.  Returns false when the platform clipboard is unavailable (e.g.
//     headless CI without a display server).
//   * Both functions are thread-compatible (calling from the main thread is
//     always safe; concurrent calls from multiple threads are NOT supported by
//     the Win32 and X11 clipboard APIs).
//
// Platform mapping:
//   * Win32  — OpenClipboard / GetClipboardData(CF_UNICODETEXT) /
//               SetClipboardData(CF_UNICODETEXT); Unicode is obtained as
//               UTF-16 and converted to UTF-8 in-process.
//   * Other  — kNotImplemented stub (returns empty / false).
//
// Dependencies: cd::core only.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <string>
#include <string_view>

namespace cd::platform
{

/// Return the clipboard text as a UTF-8 string.
/// Returns an empty string when the clipboard is empty, contains binary data,
/// or the platform clipboard is unavailable.
[[nodiscard]] std::string get_clipboard_text() noexcept;

/// Replace the clipboard with `text` (UTF-8 encoded).
/// Returns true on success, false when the platform clipboard is
/// unavailable (e.g. headless build without a display server).
[[nodiscard]] bool set_clipboard_text(std::string_view text) noexcept;

}  // namespace cd::platform
