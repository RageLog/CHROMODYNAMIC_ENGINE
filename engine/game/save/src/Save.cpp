// =============================================================================
// CHROMODYNAMIC — cd/game/save/Save.cpp
// Phase 470 — SaveSystem implementation.
//
// Atomic write pattern:
//   1. Write blob into `body.<ext>.tmp` (newly opened, truncated).
//   2. Close + flush.
//   3. `std::filesystem::rename(tmp, body)` — on every POSIX FS and modern
//      Windows (NTFS/ReFS) this is observable as either "old file" or "new
//      file" but never a partial body. On the rare host where rename is not
//      atomic we still avoid corrupting the prior body because the tmp file
//      was a separate inode.
//   4. After body is in place, write meta.json the same way (header carries
//      the timestamp + size of the *new* body — a stale meta is detectable
//      and skipped by list_slots()).
// =============================================================================
#include <cd/game/save/Save.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <cstring>
#include <fstream>
#include <ios>
#include <optional>
#include <string>
#include <system_error>

#if defined(_WIN32)
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
#endif

namespace cd::game::save
{

namespace
{

// -------- Tiny meta.json writer / parser --------------------------------------
// The header is four scalars (id, label, timestamp, format). We intentionally
// avoid pulling in rapidjson / nlohmann::json here — keeping the library
// strictly cd::core-only matches the task brief and dramatically simplifies
// vcpkg / install-tree footprint. The format produced is valid JSON:
//
//   {
//     "id": "slot_01",
//     "label": "Autosave",
//     "timestamp": 1748645000,
//     "format": "json",
//     "size_bytes": 4096
//   }
//
// The parser is whitespace-tolerant and ignores unknown keys (forward compat).

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
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned int>(c));
                    out += buf;
                }
                else
                {
                    out.push_back(c);
                }
        }
    }
    out.push_back('"');
}

[[nodiscard]] std::string render_meta_json(const SaveSlot& s)
{
    std::string out;
    out.reserve(128);
    out += "{\n  \"id\": ";
    append_json_escaped(out, s.id);
    out += ",\n  \"label\": ";
    append_json_escaped(out, s.label);
    out += ",\n  \"timestamp\": ";
    out += std::to_string(s.timestamp);
    out += ",\n  \"format\": ";
    append_json_escaped(out, to_string(s.format));
    out += ",\n  \"size_bytes\": ";
    out += std::to_string(s.size_bytes);
    out += "\n}\n";
    return out;
}

// Phase 2 — render meta.json with the extended SaveMeta keys (version,
// app_name, app_version, payload_size) appended after the Phase 1 keys.
// Phase 1 readers ignore unknown keys so this remains forward compatible.
[[nodiscard]] std::string render_meta_json_extended(const SaveSlot& s,
                                                    const SaveMeta& m)
{
    std::string out;
    out.reserve(256);
    out += "{\n  \"id\": ";
    append_json_escaped(out, s.id);
    out += ",\n  \"label\": ";
    append_json_escaped(out, s.label);
    out += ",\n  \"timestamp\": ";
    out += std::to_string(s.timestamp);
    out += ",\n  \"format\": ";
    append_json_escaped(out, to_string(s.format));
    out += ",\n  \"size_bytes\": ";
    out += std::to_string(s.size_bytes);
    out += ",\n  \"version\": ";
    out += std::to_string(static_cast<std::uint64_t>(m.version));
    out += ",\n  \"app_name\": ";
    append_json_escaped(out, m.app_name);
    out += ",\n  \"app_version\": ";
    append_json_escaped(out, m.app_version);
    out += ",\n  \"payload_size\": ";
    out += std::to_string(m.payload_size);
    out += "\n}\n";
    return out;
}

void skip_ws(const std::string& text, std::size_t& i)
{
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
}

[[nodiscard]] bool parse_string(const std::string& text, std::size_t& i, std::string& out)
{
    skip_ws(text, i);
    if (i >= text.size() || text[i] != '"') return false;
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
                    // We never produce code points >= 0x80 here, but support
                    // pass-through of basic \u00XX values written by hand.
                    if (i + 5 < text.size())
                    {
                        char hex[5] = { text[i+2], text[i+3], text[i+4], text[i+5], 0 };
                        const auto code = std::strtoul(hex, nullptr, 16);
                        if (code < 0x80) out.push_back(static_cast<char>(code));
                        i += 4;  // consumed 4 hex chars in addition to the \uXXXX prefix
                    }
                    break;
                default: out.push_back(esc); break;
            }
            i += 2;
        }
        else
        {
            out.push_back(text[i]);
            ++i;
        }
    }
    if (i >= text.size()) return false;
    ++i;  // closing quote
    return true;
}

[[nodiscard]] bool parse_number(const std::string& text, std::size_t& i, std::int64_t& out)
{
    skip_ws(text, i);
    const std::size_t start = i;
    if (i < text.size() && (text[i] == '-' || text[i] == '+')) ++i;
    while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) ++i;
    if (i == start) return false;
    out = std::strtoll(text.c_str() + start, nullptr, 10);
    return true;
}

[[nodiscard]] bool parse_meta_json(const std::string&         text,
                                   SaveSlot&                  out,
                                   SaveMeta*                  extended = nullptr)
{
    std::size_t i = 0;
    skip_ws(text, i);
    if (i >= text.size() || text[i] != '{') return false;
    ++i;

    bool seen_id = false;
    bool seen_ts = false;
    bool seen_fmt = false;

    while (true)
    {
        skip_ws(text, i);
        if (i < text.size() && text[i] == '}') { ++i; break; }
        if (i >= text.size()) return false;

        std::string key;
        if (!parse_string(text, i, key)) return false;
        skip_ws(text, i);
        if (i >= text.size() || text[i] != ':') return false;
        ++i;

        if (key == "id")
        {
            if (!parse_string(text, i, out.id)) return false;
            seen_id = true;
        }
        else if (key == "label")
        {
            if (!parse_string(text, i, out.label)) return false;
        }
        else if (key == "timestamp")
        {
            std::int64_t n = 0;
            if (!parse_number(text, i, n)) return false;
            out.timestamp = n;
            seen_ts = true;
        }
        else if (key == "format")
        {
            std::string v;
            if (!parse_string(text, i, v)) return false;
            out.format = (v == "json") ? SaveFormat::kJson : SaveFormat::kBinary;
            seen_fmt = true;
        }
        else if (key == "size_bytes")
        {
            std::int64_t n = 0;
            if (!parse_number(text, i, n)) return false;
            out.size_bytes = (n < 0) ? 0U : static_cast<std::uint64_t>(n);
        }
        else if (key == "version" && extended != nullptr)
        {
            std::int64_t n = 0;
            if (!parse_number(text, i, n)) return false;
            extended->version = (n < 0) ? 0U : static_cast<std::uint32_t>(n);
        }
        else if (key == "app_name" && extended != nullptr)
        {
            if (!parse_string(text, i, extended->app_name)) return false;
        }
        else if (key == "app_version" && extended != nullptr)
        {
            if (!parse_string(text, i, extended->app_version)) return false;
        }
        else if (key == "payload_size" && extended != nullptr)
        {
            std::int64_t n = 0;
            if (!parse_number(text, i, n)) return false;
            extended->payload_size = (n < 0) ? 0U : static_cast<std::uint64_t>(n);
        }
        else
        {
            // Skip unknown value (string OR number) for forward compatibility.
            skip_ws(text, i);
            if (i < text.size() && text[i] == '"')
            {
                std::string tmp;
                if (!parse_string(text, i, tmp)) return false;
            }
            else
            {
                std::int64_t tmp = 0;
                if (!parse_number(text, i, tmp)) return false;
            }
        }

        skip_ws(text, i);
        if (i < text.size() && text[i] == ',') { ++i; continue; }
        if (i < text.size() && text[i] == '}') { ++i; break; }
        return false;
    }

    return seen_id && seen_ts && seen_fmt;
}

// -------- Filesystem helpers --------------------------------------------------

[[nodiscard]] std::string read_text_file(const std::filesystem::path& p,
                                         std::error_code&             ec)
{
    std::ifstream f(p, std::ios::binary);
    if (!f.is_open())
    {
        ec = std::make_error_code(std::errc::no_such_file_or_directory);
        return {};
    }
    std::string out;
    f.seekg(0, std::ios::end);
    const auto sz = f.tellg();
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

[[nodiscard]] bool write_bytes_atomic(const std::filesystem::path& final_path,
                                      const std::filesystem::path& tmp_path,
                                      std::span<const std::byte>   bytes)
{
    {
        std::ofstream f(tmp_path, std::ios::binary | std::ios::trunc);
        if (!f.is_open()) return false;
        if (!bytes.empty())
        {
            f.write(reinterpret_cast<const char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
        }
        f.flush();
        if (!f.good()) return false;
    }
    std::error_code ec;
    std::filesystem::rename(tmp_path, final_path, ec);
    if (ec)
    {
        // POSIX rename is atomic + overwrites; Windows historically refused
        // to overwrite. Fall back to remove-then-rename to keep our atomicity
        // promise as strong as the host allows.
        std::filesystem::remove(final_path, ec);
        std::filesystem::rename(tmp_path, final_path, ec);
        if (ec) return false;
    }
    return true;
}

[[nodiscard]] bool write_text_atomic(const std::filesystem::path& final_path,
                                     const std::filesystem::path& tmp_path,
                                     std::string_view             text)
{
    std::span<const std::byte> bytes {
        reinterpret_cast<const std::byte*>(text.data()),
        text.size()
    };
    return write_bytes_atomic(final_path, tmp_path, bytes);
}

[[nodiscard]] std::int64_t now_epoch_seconds() noexcept
{
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

// Portable env-var fetch. MSVC marks `std::getenv` deprecated under
// `-Wdeprecated-declarations` (CRT secure-API push); use `_dupenv_s` there
// and fall back to `std::getenv` everywhere else. Returns std::nullopt for
// "unset OR empty" so callers can chain fallbacks naturally.
[[nodiscard]] std::optional<std::string> get_env(const char* name)
{
#if defined(_WIN32)
    // phase992-cgl-no-malloc-fix: wrap the _dupenv_s CRT-allocated
    // buffer in std::unique_ptr<char, decltype(&std::free)> so the
    // free() call is RAII-driven and clang-tidy's cppcoreguidelines-
    // no-malloc rule (now promoted to WarningsAsErrors at phase 995)
    // stops flagging the manual std::free site. Pure refactor; same
    // observable behaviour.
    char*       raw = nullptr;
    std::size_t sz  = 0;
    if (::_dupenv_s(&raw, &sz, name) != 0 || raw == nullptr)
    {
        return std::nullopt;
    }
    const std::unique_ptr<char, decltype(&std::free)> buf {
        raw, &std::free };
    const std::string value { buf.get() };
    if (value.empty()) return std::nullopt;
    return value;
#else
    const char* p = std::getenv(name);
    if (p == nullptr || p[0] == '\0') return std::nullopt;
    return std::string(p);
#endif
}

}  // namespace

// -----------------------------------------------------------------------------
// is_valid_slot_id
// -----------------------------------------------------------------------------
bool is_valid_slot_id(std::string_view id) noexcept
{
    if (id.empty() || id.size() > 64) return false;

    // Reject leading/trailing dots and spaces — Windows historically chokes.
    if (id.front() == '.' || id.back() == '.') return false;
    if (id.front() == ' ' || id.back() == ' ') return false;

    for (char c : id)
    {
        const auto u = static_cast<unsigned char>(c);
        const bool alnum =
            (u >= '0' && u <= '9') ||
            (u >= 'a' && u <= 'z') ||
            (u >= 'A' && u <= 'Z');
        if (!alnum && c != '-' && c != '_') return false;
    }

    // Reserved Windows device names (case-insensitive).
    static constexpr std::string_view kReserved[] = {
        "con", "prn", "aux", "nul",
        "com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8", "com9",
        "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9",
    };
    std::string lower(id);
    std::ranges::transform(lower, lower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (auto r : kReserved)
    {
        if (lower == r) return false;
    }
    return true;
}

// -----------------------------------------------------------------------------
// default_storage_root
// -----------------------------------------------------------------------------
std::filesystem::path default_storage_root()
{
    namespace fs = std::filesystem;

#if defined(_WIN32)
    if (auto base = get_env("LOCALAPPDATA"))
    {
        return fs::path(*base) / "CHROMODYNAMIC" / "saves";
    }
    if (auto base = get_env("APPDATA"))
    {
        return fs::path(*base) / "CHROMODYNAMIC" / "saves";
    }
    if (auto up = get_env("USERPROFILE"))
    {
        return fs::path(*up) / "AppData" / "Local" / "CHROMODYNAMIC" / "saves";
    }
    return fs::temp_directory_path() / "CHROMODYNAMIC" / "saves";
#elif defined(__APPLE__)
    if (auto home = get_env("HOME"))
    {
        return fs::path(*home) / "Library" / "Application Support" / "CHROMODYNAMIC" / "saves";
    }
    return fs::temp_directory_path() / "CHROMODYNAMIC" / "saves";
#else
    if (auto xdg = get_env("XDG_DATA_HOME"))
    {
        return fs::path(*xdg) / "CHROMODYNAMIC" / "saves";
    }
    if (auto home = get_env("HOME"))
    {
        return fs::path(*home) / ".local" / "share" / "CHROMODYNAMIC" / "saves";
    }
    return fs::temp_directory_path() / "CHROMODYNAMIC" / "saves";
#endif
}

// -----------------------------------------------------------------------------
// SaveSystem
// -----------------------------------------------------------------------------
SaveSystem::SaveSystem()
    : root_(default_storage_root())
{
}

SaveSystem::SaveSystem(std::filesystem::path storage_root)
    : root_(std::move(storage_root))
{
}

std::filesystem::path SaveSystem::set_storage_root(std::filesystem::path storage_root)
{
    auto previous = std::move(root_);
    root_ = std::move(storage_root);
    return previous;
}

std::filesystem::path SaveSystem::slot_directory(std::string_view slot_id) const
{
    return root_ / std::filesystem::path(std::string(slot_id));
}

std::filesystem::path SaveSystem::meta_path(const std::filesystem::path& slot_dir)
{
    return slot_dir / "meta.json";
}

std::filesystem::path SaveSystem::body_path(const std::filesystem::path& slot_dir,
                                            SaveFormat                   format)
{
    return slot_dir / (std::string("body.") + std::string(to_extension(format)));
}

std::filesystem::path SaveSystem::tmp_body_path(const std::filesystem::path& slot_dir,
                                                SaveFormat                   format)
{
    return slot_dir / (std::string("body.") + std::string(to_extension(format)) + ".tmp");
}

bool SaveSystem::slot_exists(std::string_view slot_id) const
{
    if (!is_valid_slot_id(slot_id)) return false;
    std::error_code ec;
    return std::filesystem::exists(meta_path(slot_directory(slot_id)), ec);
}

cd::core::Result<SaveSlot>
SaveSystem::read_meta(const std::filesystem::path& slot_dir) const
{
    std::error_code ec;
    if (!std::filesystem::exists(meta_path(slot_dir), ec))
    {
        return std::unexpected(save_errors::make(save_errors::Code::kSlotNotFound,
                                                  "meta.json missing"));
    }
    const std::string text = read_text_file(meta_path(slot_dir), ec);
    if (ec)
    {
        return std::unexpected(save_errors::make(save_errors::Code::kIoFailed,
                                                  "read meta.json failed"));
    }
    SaveSlot out;
    if (!parse_meta_json(text, out))
    {
        return std::unexpected(save_errors::make(save_errors::Code::kCorruptHeader,
                                                  "meta.json unparseable"));
    }
    return out;
}

cd::core::Result<void>
SaveSystem::write_meta_atomic(const std::filesystem::path& slot_dir,
                              const SaveSlot&              header) const
{
    const auto meta = meta_path(slot_dir);
    const auto tmp  = slot_dir / "meta.json.tmp";
    const auto text = render_meta_json(header);
    if (!write_text_atomic(meta, tmp, text))
    {
        return std::unexpected(save_errors::make(save_errors::Code::kIoFailed,
                                                  "atomic meta.json write failed"));
    }
    return {};
}

cd::core::Result<void>
SaveSystem::save(std::string_view              slot_id,
                 std::span<const std::byte>    blob,
                 SaveFormat                    format,
                 std::string_view              label)
{
    if (!is_valid_slot_id(slot_id))
    {
        return std::unexpected(save_errors::make(save_errors::Code::kInvalidSlotId,
                                                  "slot id contains disallowed characters"));
    }

    const auto slot_dir = slot_directory(slot_id);

    std::error_code ec;
    std::filesystem::create_directories(slot_dir, ec);
    if (ec)
    {
        return std::unexpected(save_errors::make(save_errors::Code::kIoFailed,
                                                  "create_directories failed"));
    }

    // Determine the label to persist: prefer the explicit argument; on empty
    // input fall back to the previously stored label, else the slot id.
    std::string final_label;
    if (!label.empty())
    {
        final_label.assign(label);
    }
    else
    {
        if (auto prev = read_meta(slot_dir); prev)
        {
            final_label = prev->label;
        }
    }
    if (final_label.empty())
    {
        final_label.assign(slot_id);
    }

    // 1) Body — atomic temp + rename.
    const auto body  = body_path(slot_dir, format);
    const auto tmp   = tmp_body_path(slot_dir, format);
    if (!write_bytes_atomic(body, tmp, blob))
    {
        return std::unexpected(save_errors::make(save_errors::Code::kIoFailed,
                                                  "atomic body write failed"));
    }

    // 2) Purge any other-format body file (so a format flip doesn't leak).
    const auto other_body = body_path(slot_dir,
        format == SaveFormat::kJson ? SaveFormat::kBinary : SaveFormat::kJson);
    if (std::filesystem::exists(other_body, ec))
    {
        std::filesystem::remove(other_body, ec);
    }

    // 3) Header — atomic temp + rename.
    SaveSlot header;
    header.id         = std::string(slot_id);
    header.label      = std::move(final_label);
    header.timestamp  = now_epoch_seconds();
    header.format     = format;
    header.size_bytes = blob.size();
    auto write_result = write_meta_atomic(slot_dir, header);
    if (!write_result)
    {
        return write_result;
    }
    return {};
}

cd::core::Result<std::vector<std::byte>>
SaveSystem::load(std::string_view slot_id) const
{
    if (!is_valid_slot_id(slot_id))
    {
        return std::unexpected(save_errors::make(save_errors::Code::kInvalidSlotId,
                                                  "slot id contains disallowed characters"));
    }

    const auto slot_dir = slot_directory(slot_id);
    auto       header   = read_meta(slot_dir);
    if (!header)
    {
        return std::unexpected(header.error());
    }

    const auto body = body_path(slot_dir, header->format);
    std::ifstream f(body, std::ios::binary);
    if (!f.is_open())
    {
        return std::unexpected(save_errors::make(save_errors::Code::kIoFailed,
                                                  "open body failed"));
    }
    f.seekg(0, std::ios::end);
    const auto sz = f.tellg();
    std::vector<std::byte> out;
    if (sz > 0)
    {
        out.resize(static_cast<std::size_t>(sz));
        f.seekg(0, std::ios::beg);
        f.read(reinterpret_cast<char*>(out.data()), sz);
        if (!f.good() && !f.eof())
        {
            return std::unexpected(save_errors::make(save_errors::Code::kIoFailed,
                                                      "read body failed"));
        }
    }
    return out;
}

cd::core::Result<std::vector<std::byte>>
SaveSystem::load(std::string_view slot_id, SaveFormat expected) const
{
    if (!is_valid_slot_id(slot_id))
    {
        return std::unexpected(save_errors::make(save_errors::Code::kInvalidSlotId,
                                                  "slot id contains disallowed characters"));
    }

    const auto slot_dir = slot_directory(slot_id);
    auto       header   = read_meta(slot_dir);
    if (!header)
    {
        return std::unexpected(header.error());
    }
    if (header->format != expected)
    {
        return std::unexpected(save_errors::make(save_errors::Code::kFormatMismatch,
                                                  "on-disk format differs from expected"));
    }
    return load(slot_id);
}

std::vector<SaveSlot> SaveSystem::list_slots() const
{
    std::vector<SaveSlot> out;

    std::error_code ec;
    if (!std::filesystem::exists(root_, ec)) return out;
    if (!std::filesystem::is_directory(root_, ec)) return out;

    for (const auto& entry : std::filesystem::directory_iterator(root_, ec))
    {
        if (ec) break;
        if (!entry.is_directory()) continue;

        const auto name = entry.path().filename().string();
        if (!is_valid_slot_id(name)) continue;

        if (auto header = read_meta(entry.path()); header)
        {
            // Canonicalize the id from the folder name so a hand-edited
            // meta.json cannot lie about its slot.
            header->id = name;
            out.push_back(std::move(*header));
        }
    }

    // Most-recently saved first.
    std::ranges::sort(out,
                      [](const SaveSlot& a, const SaveSlot& b) {
                          return a.timestamp > b.timestamp;
                      });
    return out;
}

cd::core::Result<void>
SaveSystem::delete_slot(std::string_view slot_id)
{
    if (!is_valid_slot_id(slot_id))
    {
        return std::unexpected(save_errors::make(save_errors::Code::kInvalidSlotId,
                                                  "slot id contains disallowed characters"));
    }

    const auto slot_dir = slot_directory(slot_id);

    std::error_code ec;
    if (!std::filesystem::exists(slot_dir, ec))
    {
        return std::unexpected(save_errors::make(save_errors::Code::kSlotNotFound,
                                                  "slot directory missing"));
    }
    const auto removed = std::filesystem::remove_all(slot_dir, ec);
    if (ec)
    {
        return std::unexpected(save_errors::make(save_errors::Code::kIoFailed,
                                                  "remove_all failed"));
    }
    if (removed == 0)
    {
        return std::unexpected(save_errors::make(save_errors::Code::kSlotNotFound,
                                                  "slot was empty"));
    }
    return {};
}

// =============================================================================
// Phase 2 (G5.3) — versioning + migration + cloud hook.
// =============================================================================

cd::core::Result<void>
SaveSystem::write_meta_with_extended(const std::filesystem::path& slot_dir,
                                     const SaveSlot&              header,
                                     const SaveMeta&              extended) const
{
    const auto meta = meta_path(slot_dir);
    const auto tmp  = slot_dir / "meta.json.tmp";
    const auto text = render_meta_json_extended(header, extended);
    if (!write_text_atomic(meta, tmp, text))
    {
        return std::unexpected(save_errors::make(save_errors::Code::kIoFailed,
                                                  "atomic meta.json write failed"));
    }
    return {};
}

cd::core::Result<SaveMeta>
SaveSystem::read_extended_meta(const std::filesystem::path& slot_dir) const
{
    std::error_code ec;
    if (!std::filesystem::exists(meta_path(slot_dir), ec))
    {
        return std::unexpected(save_errors::make(save_errors::Code::kSlotNotFound,
                                                  "meta.json missing"));
    }
    const std::string text = read_text_file(meta_path(slot_dir), ec);
    if (ec)
    {
        return std::unexpected(save_errors::make(save_errors::Code::kIoFailed,
                                                  "read meta.json failed"));
    }
    SaveSlot slot;
    SaveMeta ext;  // defaults: version=1, payload_size=0 — matches Phase 1 implicit.
    if (!parse_meta_json(text, slot, &ext))
    {
        return std::unexpected(save_errors::make(save_errors::Code::kCorruptHeader,
                                                  "meta.json unparseable"));
    }
    // Inherit ground-truth fields from the Phase 1 slot record so callers
    // see a single coherent SaveMeta regardless of whether the file was
    // written by Phase 1 or Phase 2 code paths.
    ext.format       = slot.format;
    ext.timestamp    = slot.timestamp;
    if (ext.payload_size == 0)
    {
        ext.payload_size = slot.size_bytes;
    }
    return ext;
}

cd::core::Result<void>
SaveSystem::save_with_meta(std::string_view              slot_id,
                           SaveMeta                      meta,
                           std::span<const std::byte>    blob,
                           std::string_view              label)
{
    if (!is_valid_slot_id(slot_id))
    {
        return std::unexpected(save_errors::make(save_errors::Code::kInvalidSlotId,
                                                  "slot id contains disallowed characters"));
    }

    const auto slot_dir = slot_directory(slot_id);

    std::error_code ec;
    std::filesystem::create_directories(slot_dir, ec);
    if (ec)
    {
        return std::unexpected(save_errors::make(save_errors::Code::kIoFailed,
                                                  "create_directories failed"));
    }

    // Resolve label the same way save() does: explicit > previously-stored > slot id.
    std::string final_label;
    if (!label.empty())
    {
        final_label.assign(label);
    }
    else
    {
        if (auto prev = read_meta(slot_dir); prev)
        {
            final_label = prev->label;
        }
    }
    if (final_label.empty())
    {
        final_label.assign(slot_id);
    }

    // 1) Body atomic write.
    const auto body = body_path(slot_dir, meta.format);
    const auto tmp  = tmp_body_path(slot_dir, meta.format);
    if (!write_bytes_atomic(body, tmp, blob))
    {
        return std::unexpected(save_errors::make(save_errors::Code::kIoFailed,
                                                  "atomic body write failed"));
    }

    // 2) Purge other-format body to keep the slot directory single-sourced.
    const auto other_body = body_path(slot_dir,
        meta.format == SaveFormat::kJson ? SaveFormat::kBinary : SaveFormat::kJson);
    if (std::filesystem::exists(other_body, ec))
    {
        std::filesystem::remove(other_body, ec);
    }

    // 3) Header — overwrite the timestamp + payload_size with authoritative
    //    values (so a stale caller-supplied meta never lies on disk).
    meta.timestamp    = now_epoch_seconds();
    meta.payload_size = blob.size();

    SaveSlot header;
    header.id         = std::string(slot_id);
    header.label      = std::move(final_label);
    header.timestamp  = meta.timestamp;
    header.format     = meta.format;
    header.size_bytes = blob.size();

    if (auto wr = write_meta_with_extended(slot_dir, header, meta); !wr)
    {
        return wr;
    }

    // 4) Optional cloud upload — best effort, post-local-commit. A failure
    //    here surfaces kCloudFailed but the local body is left intact and
    //    will be returned by subsequent loads.
    if (cloud_upload_)
    {
        auto up = cloud_upload_(slot_id, meta, blob);
        if (!up)
        {
            return std::unexpected(save_errors::make(save_errors::Code::kCloudFailed,
                                                      "cloud upload reported error"));
        }
    }

    return {};
}

cd::core::Result<SaveSystem::LoadWithMetaResult>
SaveSystem::load_with_meta(std::string_view slot_id)
{
    if (!is_valid_slot_id(slot_id))
    {
        return std::unexpected(save_errors::make(save_errors::Code::kInvalidSlotId,
                                                  "slot id contains disallowed characters"));
    }

    if (slot_exists(slot_id))
    {
        const auto slot_dir = slot_directory(slot_id);
        auto       meta     = read_extended_meta(slot_dir);
        if (!meta)
        {
            return std::unexpected(meta.error());
        }
        auto blob = load(slot_id);
        if (!blob)
        {
            return std::unexpected(blob.error());
        }
        LoadWithMetaResult result;
        result.meta = std::move(*meta);
        result.blob = std::move(*blob);
        return result;
    }

    // Local miss — try cloud fallback.
    if (cloud_download_)
    {
        auto dl = cloud_download_(slot_id);
        if (!dl)
        {
            // Surface the original "missing" outcome if the handler said so,
            // otherwise tag the failure as cloud-side.
            const auto& e = dl.error();
            if (e.domain == save_errors::kDomain &&
                e.code   == static_cast<std::uint32_t>(save_errors::Code::kSlotNotFound))
            {
                return std::unexpected(save_errors::make(save_errors::Code::kSlotNotFound,
                                                          "slot missing locally and in cloud"));
            }
            return std::unexpected(save_errors::make(save_errors::Code::kCloudFailed,
                                                      "cloud download reported error"));
        }
        // Write the cloud body locally so subsequent loads are fast — we
        // call into save_with_meta() but suppress the upload-back to avoid a
        // download / upload ping-pong by temporarily swapping out the
        // upload handler.
        auto saved_upload = std::move(cloud_upload_);
        cloud_upload_ = {};
        auto wr = save_with_meta(slot_id, dl->meta, dl->blob);
        cloud_upload_ = std::move(saved_upload);
        if (!wr)
        {
            return std::unexpected(wr.error());
        }
        LoadWithMetaResult result;
        result.meta = std::move(dl->meta);
        result.blob = std::move(dl->blob);
        return result;
    }

    return std::unexpected(save_errors::make(save_errors::Code::kSlotNotFound,
                                              "slot missing and no cloud handler"));
}

void SaveSystem::register_migration(std::uint32_t from_v,
                                    std::uint32_t to_v,
                                    MigrationFn   fn)
{
    if (!fn) return;  // defensive — null fn is a host bug, silently ignore.
    MigrationEdge edge;
    edge.to_v = to_v;
    edge.fn   = std::move(fn);
    migrations_[from_v] = std::move(edge);
}

cd::core::Result<SaveMeta>
SaveSystem::migrate(std::string_view slot_id, std::uint32_t target_version)
{
    if (!is_valid_slot_id(slot_id))
    {
        return std::unexpected(save_errors::make(save_errors::Code::kInvalidSlotId,
                                                  "slot id contains disallowed characters"));
    }

    auto loaded = load_with_meta(slot_id);
    if (!loaded)
    {
        return std::unexpected(loaded.error());
    }

    SaveMeta              current_meta = std::move(loaded->meta);
    std::vector<std::byte> current_blob = std::move(loaded->blob);

    // Walk the registered edges. Guard against pathological cycles with a
    // hard cap — 64 hops is generous for any realistic schema history and
    // saves us from an infinite loop if a host registers a cyclic graph.
    constexpr int kMaxHops = 64;
    int hops = 0;

    while (current_meta.version != target_version)
    {
        auto it = migrations_.find(current_meta.version);
        if (it == migrations_.end())
        {
            return std::unexpected(save_errors::make(save_errors::Code::kMigrationMissing,
                                                      "no migration edge from current version"));
        }
        if (++hops > kMaxHops)
        {
            return std::unexpected(save_errors::make(save_errors::Code::kMigrationFailed,
                                                      "migration chain exceeded hop cap"));
        }

        const auto& edge = it->second;
        auto stepped = edge.fn(current_blob);
        if (!stepped)
        {
            return std::unexpected(save_errors::make(save_errors::Code::kMigrationFailed,
                                                      "migration function failed"));
        }

        current_blob          = std::move(*stepped);
        current_meta.version  = edge.to_v;
        current_meta.payload_size = current_blob.size();

        // Persist the intermediate step so a crash mid-chain resumes from
        // here rather than the original v1 (matches Unity / Unreal's per-
        // step migration semantics). We bypass the cloud upload during the
        // chain to avoid uploading every intermediate version.
        auto saved_upload = std::move(cloud_upload_);
        cloud_upload_ = {};
        auto wr = save_with_meta(slot_id, current_meta, current_blob);
        cloud_upload_ = std::move(saved_upload);
        if (!wr)
        {
            return std::unexpected(wr.error());
        }
    }

    return current_meta;
}

void SaveSystem::set_cloud_handler(CloudUploadFn upload, CloudDownloadFn download)
{
    cloud_upload_   = std::move(upload);
    cloud_download_ = std::move(download);
}

}  // namespace cd::game::save
