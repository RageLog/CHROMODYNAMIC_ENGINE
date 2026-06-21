// =============================================================================
// CHROMODYNAMIC — cd/platform/Clipboard.cpp
// Win32 clipboard get/set via OpenClipboard / GetClipboardData(CF_UNICODETEXT).
// Non-Win32 hosts get the no-op stub at the bottom of this file.
// =============================================================================
#include <cd/platform/Clipboard.hpp>

#if defined(_WIN32)

    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>

    #include <algorithm>
    #include <string>
    #include <string_view>

namespace cd::platform
{

namespace
{

// ---- UTF-16 <-> UTF-8 helpers -----------------------------------------------

[[nodiscard]] std::string utf16_to_utf8(const wchar_t* ws) noexcept
{
    if (ws == nullptr || ws[0] == L'\0')
        return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, ws, -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 0)
        return {};
    std::string out(static_cast<std::size_t>(needed - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws, -1, out.data(), needed, nullptr, nullptr);
    return out;
}

[[nodiscard]] std::wstring utf8_to_utf16(std::string_view sv) noexcept
{
    if (sv.empty())
        return {};
    // MB_ERR_INVALID_CHARS: without it MultiByteToWideChar is lenient and
    // substitutes U+FFFD for malformed UTF-8 (never failing), which would let
    // set_clipboard_text's empty-result guard never fire on bad input. With the
    // flag, invalid byte sequences make the call return 0 -> empty wstring.
    const int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                           sv.data(), static_cast<int>(sv.size()), nullptr, 0);
    if (needed <= 0)
        return {};
    std::wstring out(static_cast<std::size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                        sv.data(), static_cast<int>(sv.size()), out.data(), needed);
    return out;
}

}  // namespace

std::string get_clipboard_text() noexcept
{
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT))
        return {};
    if (!OpenClipboard(nullptr))
        return {};
    std::string result;
    const auto h = GetClipboardData(CF_UNICODETEXT);
    if (h != nullptr)
    {
        const auto* ws = static_cast<const wchar_t*>(GlobalLock(h));
        if (ws != nullptr)
        {
            result = utf16_to_utf8(ws);
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return result;
}

bool set_clipboard_text(std::string_view text) noexcept
{
    if (!OpenClipboard(nullptr))
        return false;
    const std::wstring wide = utf8_to_utf16(text);
    // Guard: non-empty input that failed UTF-8 → UTF-16 conversion (e.g. invalid
    // byte sequences) must not silently wipe the clipboard and return true.
    // Empty input is the legitimate "clear clipboard" path and is allowed through.
    if (wide.empty() && !text.empty())
    {
        CloseClipboard();
        return false;
    }

    EmptyClipboard();

    // Allocate GMEM_MOVEABLE block (CF_UNICODETEXT requirement): size includes
    // the null terminator.
    const std::size_t byte_count = (wide.size() + 1U) * sizeof(wchar_t);
    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, byte_count);
    if (hg == nullptr)
    {
        CloseClipboard();
        return false;
    }
    auto* dst = static_cast<wchar_t*>(GlobalLock(hg));
    if (dst == nullptr)
    {
        GlobalFree(hg);
        CloseClipboard();
        return false;
    }
    std::ranges::copy(wide, dst);
    dst[wide.size()] = L'\0';
    GlobalUnlock(hg);

    const bool ok = SetClipboardData(CF_UNICODETEXT, hg) != nullptr;
    // On success, the clipboard owns `hg`; on failure we must free it.
    if (!ok)
        GlobalFree(hg);
    CloseClipboard();
    return ok;
}

}  // namespace cd::platform

#else  // !_WIN32

namespace cd::platform
{

std::string get_clipboard_text() noexcept
{
    return {};
}

bool set_clipboard_text(std::string_view /*text*/) noexcept
{
    return false;
}

}  // namespace cd::platform

#endif
