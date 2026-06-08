// =============================================================================
// CHROMODYNAMIC — cd/editor/cdproj/CdprojFile.cpp
//
// phase547 — .cdproj project-file implementation.
//
// JSON is hand-rolled to keep the library cd::core-only (no nlohmann / no
// rapidjson), consistent with cd::game_save and CLAUDE.md §6.
//
// The parser is whitespace-tolerant and ignores unknown keys for forward
// compatibility with future schema_version 2+ fields. It only requires
// schema_version == 1.
// =============================================================================
#include <cd/editor/cdproj/CdprojFile.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <filesystem>
#include <fstream>
#include <ios>
#include <ranges>
#include <string_view>
#include <system_error>

#if defined(_WIN32)
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
#endif

namespace cd::editor::cdproj
{

namespace
{

// ===========================================================================
// Tiny hand-rolled JSON helpers (write path)
// ===========================================================================

void append_json_escaped(std::string& out, std::string_view value)
{
    out.push_back('"');
    for (char c : value)
    {
        switch (c)
        {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20)
                {
                    char buf[8] {};
                    std::snprintf(buf, sizeof(buf), "\\u%04x",
                                  static_cast<unsigned int>(
                                      static_cast<unsigned char>(c)));
                    out += buf;
                }
                else
                {
                    out.push_back(c);
                }
                break;
        }
    }
    out.push_back('"');
}

[[nodiscard]] std::string render_cdproj_json(const CdprojData& d)
{
    std::string out;
    out.reserve(512);
    out += "{\n";

    out += "  \"schema_version\": ";
    out += std::to_string(d.schema_version);
    out += ",\n";

    out += "  \"last_opened_scene_path\": ";
    append_json_escaped(out, d.last_opened_scene_path);
    out += ",\n";

    out += "  \"dock_layout\": ";
    append_json_escaped(out, d.dock_layout);
    out += ",\n";

    out += "  \"window\": {\n";
    out += "    \"x\": ";
    out += std::to_string(d.window.x);
    out += ",\n    \"y\": ";
    out += std::to_string(d.window.y);
    out += ",\n    \"w\": ";
    out += std::to_string(d.window.w);
    out += ",\n    \"h\": ";
    out += std::to_string(d.window.h);
    out += ",\n    \"maximized\": ";
    out += d.window.maximized ? "true" : "false";
    out += "\n  },\n";

    out += "  \"recent_files\": [";
    for (std::size_t i = 0; i < d.recent_files.size(); ++i)
    {
        if (i > 0) { out += ", "; }
        append_json_escaped(out, d.recent_files[i]);
    }
    out += "],\n";

    // phase695 / M14 W6A — active theme name.
    out += "  \"theme_name\": ";
    append_json_escaped(out, d.theme_name.empty() ? "dark" : d.theme_name);
    out += ",\n";

    // phase788 / H5 — Custom layout panel selection (empty array when absent).
    out += "  \"user_custom_layout\": [";
    for (std::size_t i = 0; i < d.user_custom_layout.size(); ++i)
    {
        if (i > 0) { out += ", "; }
        append_json_escaped(out, d.user_custom_layout[i]);
    }
    out += "]\n";

    out += "}\n";
    return out;
}

// ===========================================================================
// Tiny hand-rolled JSON helpers (read/parse path)
// ===========================================================================

void skip_ws(std::string_view text, std::size_t& i) noexcept
{
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])))
    {
        ++i;
    }
}

[[nodiscard]] bool parse_string(std::string_view text, std::size_t& i,
                                std::string& out)
{
    skip_ws(text, i);
    if (i >= text.size() || text[i] != '"') { return false; }
    ++i;
    out.clear();
    while (i < text.size() && text[i] != '"')
    {
        if (text[i] == '\\' && i + 1 < text.size())
        {
            const char esc = text[i + 1];
            switch (esc)
            {
                case '"':  out.push_back('"');  break;
                case '\\': out.push_back('\\'); break;
                case '/':  out.push_back('/');  break;
                case 'b':  out.push_back('\b'); break;
                case 'f':  out.push_back('\f'); break;
                case 'n':  out.push_back('\n'); break;
                case 'r':  out.push_back('\r'); break;
                case 't':  out.push_back('\t'); break;
                case 'u':
                    if (i + 5 < text.size())
                    {
                        char hex[5] = {
                            text[i + 2], text[i + 3],
                            text[i + 4], text[i + 5], '\0'
                        };
                        const auto code = std::strtoul(hex, nullptr, 16);
                        if (code < 0x80)
                        {
                            out.push_back(static_cast<char>(code));
                        }
                        i += 4;
                    }
                    break;
                default:
                    out.push_back(esc);
                    break;
            }
            i += 2;
        }
        else
        {
            out.push_back(text[i]);
            ++i;
        }
    }
    if (i >= text.size()) { return false; }
    ++i;  // consume closing '"'
    return true;
}

[[nodiscard]] bool parse_int(std::string_view text, std::size_t& i,
                             std::int64_t& out)
{
    skip_ws(text, i);
    const std::size_t start = i;
    if (i < text.size() && (text[i] == '-' || text[i] == '+')) { ++i; }
    while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])))
    {
        ++i;
    }
    if (i == start) { return false; }
    out = std::strtoll(text.data() + start, nullptr, 10);
    return true;
}

[[nodiscard]] bool parse_bool(std::string_view text, std::size_t& i,
                              bool& out)
{
    skip_ws(text, i);
    if (text.substr(i, 4) == "true")
    {
        out = true;
        i  += 4;
        return true;
    }
    if (text.substr(i, 5) == "false")
    {
        out = false;
        i  += 5;
        return true;
    }
    return false;
}

// Skip an arbitrary value (string, number, bool, null, nested object/array).
// Used for forward-compat ignoring of unknown keys.
[[nodiscard]] bool skip_value(std::string_view text, std::size_t& i);

[[nodiscard]] bool skip_value(std::string_view text, std::size_t& i)
{
    skip_ws(text, i);
    if (i >= text.size()) { return false; }

    if (text[i] == '"')
    {
        std::string tmp;
        return parse_string(text, i, tmp);
    }
    if (text[i] == '{')
    {
        ++i;
        while (true)
        {
            skip_ws(text, i);
            if (i >= text.size()) { return false; }
            if (text[i] == '}') { ++i; return true; }
            std::string k;
            if (!parse_string(text, i, k)) { return false; }
            skip_ws(text, i);
            if (i >= text.size() || text[i] != ':') { return false; }
            ++i;
            if (!skip_value(text, i)) { return false; }
            skip_ws(text, i);
            if (i < text.size() && text[i] == ',') { ++i; }
        }
    }
    if (text[i] == '[')
    {
        ++i;
        while (true)
        {
            skip_ws(text, i);
            if (i >= text.size()) { return false; }
            if (text[i] == ']') { ++i; return true; }
            if (!skip_value(text, i)) { return false; }
            skip_ws(text, i);
            if (i < text.size() && text[i] == ',') { ++i; }
        }
    }
    if (text.substr(i, 4) == "null")  { i += 4; return true; }
    if (text.substr(i, 4) == "true")  { i += 4; return true; }
    if (text.substr(i, 5) == "false") { i += 5; return true; }
    // number
    {
        std::int64_t tmp = 0;
        return parse_int(text, i, tmp);
    }
}

// Parse the "window" sub-object.
[[nodiscard]] bool parse_window_object(std::string_view text, std::size_t& i,
                                       CdprojWindow& out)
{
    skip_ws(text, i);
    if (i >= text.size() || text[i] != '{') { return false; }
    ++i;

    while (true)
    {
        skip_ws(text, i);
        if (i >= text.size()) { return false; }
        if (text[i] == '}') { ++i; break; }

        std::string key;
        if (!parse_string(text, i, key)) { return false; }
        skip_ws(text, i);
        if (i >= text.size() || text[i] != ':') { return false; }
        ++i;

        if (key == "x")
        {
            std::int64_t n = 0;
            if (!parse_int(text, i, n)) { return false; }
            out.x = static_cast<int>(n);
        }
        else if (key == "y")
        {
            std::int64_t n = 0;
            if (!parse_int(text, i, n)) { return false; }
            out.y = static_cast<int>(n);
        }
        else if (key == "w")
        {
            std::int64_t n = 0;
            if (!parse_int(text, i, n)) { return false; }
            out.w = static_cast<int>(n);
        }
        else if (key == "h")
        {
            std::int64_t n = 0;
            if (!parse_int(text, i, n)) { return false; }
            out.h = static_cast<int>(n);
        }
        else if (key == "maximized")
        {
            if (!parse_bool(text, i, out.maximized)) { return false; }
        }
        else
        {
            if (!skip_value(text, i)) { return false; }
        }

        skip_ws(text, i);
        if (i < text.size() && text[i] == ',') { ++i; continue; }
        if (i < text.size() && text[i] == '}') { ++i; break; }
        return false;
    }
    return true;
}

// Parse the "recent_files" JSON array.
[[nodiscard]] bool parse_string_array(std::string_view text, std::size_t& i,
                                      std::vector<std::string>& out)
{
    skip_ws(text, i);
    if (i >= text.size() || text[i] != '[') { return false; }
    ++i;

    while (true)
    {
        skip_ws(text, i);
        if (i >= text.size()) { return false; }
        if (text[i] == ']') { ++i; break; }

        std::string entry;
        if (!parse_string(text, i, entry)) { return false; }
        out.push_back(std::move(entry));

        skip_ws(text, i);
        if (i < text.size() && text[i] == ',') { ++i; continue; }
        if (i < text.size() && text[i] == ']') { ++i; break; }
        return false;
    }
    return true;
}

[[nodiscard]] bool parse_cdproj_json(std::string_view text, CdprojData& out)
{
    std::size_t i = 0;
    skip_ws(text, i);
    if (i >= text.size() || text[i] != '{') { return false; }
    ++i;

    bool seen_schema_version = false;

    while (true)
    {
        skip_ws(text, i);
        if (i >= text.size()) { return false; }
        if (text[i] == '}') { ++i; break; }

        std::string key;
        if (!parse_string(text, i, key)) { return false; }
        skip_ws(text, i);
        if (i >= text.size() || text[i] != ':') { return false; }
        ++i;

        if (key == "schema_version")
        {
            std::int64_t n = 0;
            if (!parse_int(text, i, n)) { return false; }
            out.schema_version = static_cast<int>(n);
            seen_schema_version = true;
        }
        else if (key == "last_opened_scene_path")
        {
            if (!parse_string(text, i, out.last_opened_scene_path))
            {
                return false;
            }
        }
        else if (key == "dock_layout")
        {
            if (!parse_string(text, i, out.dock_layout)) { return false; }
        }
        else if (key == "window")
        {
            if (!parse_window_object(text, i, out.window)) { return false; }
        }
        else if (key == "recent_files")
        {
            if (!parse_string_array(text, i, out.recent_files)) { return false; }
        }
        else if (key == "theme_name")
        {
            // phase695 / M14 W6A — active theme name; optional field.
            if (!parse_string(text, i, out.theme_name)) { return false; }
        }
        else if (key == "user_custom_layout")
        {
            // phase788 / H5 — Custom layout panel list; optional field.
            if (!parse_string_array(text, i, out.user_custom_layout))
            {
                return false;
            }
        }
        else
        {
            // Unknown key — skip value for forward compatibility.
            if (!skip_value(text, i)) { return false; }
        }

        skip_ws(text, i);
        if (i < text.size() && text[i] == ',') { ++i; continue; }
        if (i < text.size() && text[i] == '}') { ++i; break; }
        return false;
    }

    return seen_schema_version;
}

// ===========================================================================
// Filesystem helpers
// ===========================================================================

[[nodiscard]] std::string read_text_file(const std::filesystem::path& p,
                                         std::error_code&             ec)
{
    std::ifstream f(p, std::ios::binary);
    if (!f.is_open())
    {
        ec = std::make_error_code(std::errc::no_such_file_or_directory);
        return {};
    }
    f.seekg(0, std::ios::end);
    const auto sz = f.tellg();
    std::string out;
    if (sz > 0)
    {
        out.resize(static_cast<std::size_t>(sz));
        f.seekg(0, std::ios::beg);
        f.read(out.data(), sz);
    }
    if (!f.good() && !f.eof())
    {
        ec = std::make_error_code(std::errc::io_error);
        return {};
    }
    ec.clear();
    return out;
}

[[nodiscard]] bool write_text_atomic(const std::filesystem::path& final_path,
                                     const std::string&           text)
{
    const auto tmp_path = std::filesystem::path(final_path) += ".tmp";
    {
        std::ofstream f(tmp_path, std::ios::binary | std::ios::trunc);
        if (!f.is_open()) { return false; }
        f.write(text.data(), static_cast<std::streamsize>(text.size()));
        f.flush();
        if (!f.good()) { return false; }
    }
    std::error_code ec;
    std::filesystem::rename(tmp_path, final_path, ec);
    if (ec)
    {
        // Windows historically refuses to rename over an existing file; fall
        // back to remove-then-rename (best-effort atomicity on Windows).
        std::filesystem::remove(final_path, ec);
        std::filesystem::rename(tmp_path, final_path, ec);
        if (ec) { return false; }
    }
    return true;
}

// Portable getenv — mirrors cd::game_save pattern with RAII wrapper.
[[nodiscard]] std::optional<std::string> get_env(const char* name)
{
#if defined(_WIN32)
    // phase992-cgl-no-malloc-fix: same RAII pattern as
    // engine/game/save/src/Save.cpp -- _dupenv_s allocates from
    // the CRT, must be freed with std::free, wrap in unique_ptr
    // so cppcoreguidelines-no-malloc stops flagging the manual
    // std::free call.
    char*       raw = nullptr;
    std::size_t sz  = 0;
    if (::_dupenv_s(&raw, &sz, name) != 0 || raw == nullptr)
    {
        return std::nullopt;
    }
    const std::unique_ptr<char, decltype(&std::free)> buf {
        raw, &std::free };
    const std::string value { buf.get() };
    if (value.empty()) { return std::nullopt; }
    return value;
#else
    const char* p = std::getenv(name);
    if (p == nullptr || p[0] == '\0') { return std::nullopt; }
    return std::string(p);
#endif
}

}  // namespace

// ===========================================================================
// Public API implementation
// ===========================================================================

std::optional<CdprojData> read_cdproj(const std::filesystem::path& path)
{
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) { return std::nullopt; }

    const std::string text = read_text_file(path, ec);
    if (ec) { return std::nullopt; }

    CdprojData data;
    if (!parse_cdproj_json(text, data)) { return std::nullopt; }

    // Only schema_version == 1 is supported.
    if (data.schema_version != 1) { return std::nullopt; }

    return data;
}

bool write_cdproj(const CdprojData& data, const std::filesystem::path& path)
{
    // Ensure parent directory exists.
    std::error_code ec;
    const auto parent = path.parent_path();
    if (!parent.empty() && !std::filesystem::exists(parent, ec))
    {
        std::filesystem::create_directories(parent, ec);
        if (ec) { return false; }
    }

    const std::string json = render_cdproj_json(data);
    return write_text_atomic(path, json);
}

void push_recent_file(CdprojData& data, std::string path)
{
    // Move-to-front deduplication.
    auto it = std::ranges::find(data.recent_files, path);
    if (it != data.recent_files.end())
    {
        data.recent_files.erase(it);
    }
    data.recent_files.insert(data.recent_files.begin(), std::move(path));

    // FIFO eviction: keep at most 10 entries.
    static constexpr std::size_t kMaxRecentFiles = 10;
    if (data.recent_files.size() > kMaxRecentFiles)
    {
        data.recent_files.resize(kMaxRecentFiles);
    }
}

std::filesystem::path default_cdproj_path()
{
    namespace fs = std::filesystem;

#if defined(_WIN32)
    if (auto base = get_env("APPDATA"))
    {
        return fs::path(*base) / "cd_editor" / "last.cdproj";
    }
    if (auto base = get_env("LOCALAPPDATA"))
    {
        return fs::path(*base) / "cd_editor" / "last.cdproj";
    }
    if (auto up = get_env("USERPROFILE"))
    {
        return fs::path(*up) / "AppData" / "Roaming" / "cd_editor" / "last.cdproj";
    }
    return fs::temp_directory_path() / "cd_editor" / "last.cdproj";
#elif defined(__APPLE__)
    if (auto home = get_env("HOME"))
    {
        return fs::path(*home) / "Library" / "Application Support"
                              / "cd_editor" / "last.cdproj";
    }
    return fs::temp_directory_path() / "cd_editor" / "last.cdproj";
#else
    if (auto xdg = get_env("XDG_CONFIG_HOME"))
    {
        return fs::path(*xdg) / "cd_editor" / "last.cdproj";
    }
    if (auto home = get_env("HOME"))
    {
        return fs::path(*home) / ".config" / "cd_editor" / "last.cdproj";
    }
    return fs::temp_directory_path() / "cd_editor" / "last.cdproj";
#endif
}

}  // namespace cd::editor::cdproj
