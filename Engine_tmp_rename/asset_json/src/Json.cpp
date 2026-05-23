// =============================================================================
// CHROMODYNAMIC — cd/asset_json/Json.cpp
// =============================================================================
#include <cd/asset_json/Json.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <ios>
#include <sstream>
#include <string>
#include <string_view>

namespace cd::asset_json
{

cd::core::Result<const Value*> Value::at(std::string_view key) const
{
    if (!is_object())
        return std::unexpected(json_errors::make(json_errors::Code::kTypeMismatch, "Value is not an object"));
    const auto& obj = as_object();
    const auto it = obj.find(std::string { key });
    if (it == obj.end())
        return std::unexpected(json_errors::make(json_errors::Code::kKeyNotFound, key));
    return &it->second;
}

cd::core::Result<const Value*> Value::at(std::size_t index) const
{
    if (!is_array())
        return std::unexpected(json_errors::make(json_errors::Code::kTypeMismatch, "Value is not an array"));
    const auto& arr = as_array();
    if (index >= arr.size())
        return std::unexpected(json_errors::make(json_errors::Code::kKeyNotFound, "array index OOB"));
    return &arr[index];
}

namespace
{

class Parser
{
public:
    Parser(std::string_view text, std::uint32_t max_depth) noexcept
        : text_ { text }
        , max_depth_ { max_depth }
    {
    }

    cd::core::Result<Value> parse_root()
    {
        skip_ws();
        auto v = parse_value(0);
        if (!v.has_value())
            return std::unexpected(v.error());
        skip_ws();
        if (pos_ != text_.size())
            return std::unexpected(
                json_errors::make(json_errors::Code::kUnexpectedToken, "trailing characters after JSON value")
            );
        return v;
    }

private:
    cd::core::Result<Value> parse_value(std::uint32_t depth)
    {
        if (depth > max_depth_)
            return std::unexpected(json_errors::make(json_errors::Code::kDepthLimit, "nesting exceeds max_depth"));
        skip_ws();
        if (pos_ >= text_.size())
            return std::unexpected(json_errors::make(json_errors::Code::kUnexpectedToken, "unexpected end of input"));
        const char c = text_[pos_];
        if (c == '{')
            return parse_object(depth);
        if (c == '[')
            return parse_array(depth);
        if (c == '"')
        {
            auto s = parse_string();
            if (!s.has_value())
                return std::unexpected(s.error());
            return Value { std::move(*s) };
        }
        if (c == 't' || c == 'f')
            return parse_bool();
        if (c == 'n')
            return parse_null();
        // number: -, digit, .
        if (c == '-' || (c >= '0' && c <= '9'))
            return parse_number();
        return std::unexpected(
            json_errors::make(json_errors::Code::kUnexpectedToken, std::string { "unexpected character: '" } + c + "'")
        );
    }

    cd::core::Result<Value> parse_object(std::uint32_t depth)
    {
        ++pos_;  // '{'
        Object obj;
        skip_ws();
        if (pos_ < text_.size() && text_[pos_] == '}')
        {
            ++pos_;
            return Value { std::move(obj) };
        }
        while (true)
        {
            skip_ws();
            auto key = parse_string();
            if (!key.has_value())
                return std::unexpected(key.error());
            skip_ws();
            if (pos_ >= text_.size() || text_[pos_] != ':')
                return std::unexpected(
                    json_errors::make(json_errors::Code::kUnexpectedToken, "expected ':' in object")
                );
            ++pos_;
            auto val = parse_value(depth + 1);
            if (!val.has_value())
                return std::unexpected(val.error());
            obj[std::move(*key)] = std::move(*val);
            skip_ws();
            if (pos_ >= text_.size())
                return std::unexpected(json_errors::make(json_errors::Code::kUnexpectedToken, "unterminated object"));
            if (text_[pos_] == ',')
            {
                ++pos_;
                continue;
            }
            if (text_[pos_] == '}')
            {
                ++pos_;
                return Value { std::move(obj) };
            }
            return std::unexpected(
                json_errors::make(json_errors::Code::kUnexpectedToken, "expected ',' or '}' in object")
            );
        }
    }

    cd::core::Result<Value> parse_array(std::uint32_t depth)
    {
        ++pos_;  // '['
        Array arr;
        skip_ws();
        if (pos_ < text_.size() && text_[pos_] == ']')
        {
            ++pos_;
            return Value { std::move(arr) };
        }
        while (true)
        {
            auto v = parse_value(depth + 1);
            if (!v.has_value())
                return std::unexpected(v.error());
            arr.push_back(std::move(*v));
            skip_ws();
            if (pos_ >= text_.size())
                return std::unexpected(json_errors::make(json_errors::Code::kUnexpectedToken, "unterminated array"));
            if (text_[pos_] == ',')
            {
                ++pos_;
                continue;
            }
            if (text_[pos_] == ']')
            {
                ++pos_;
                return Value { std::move(arr) };
            }
            return std::unexpected(
                json_errors::make(json_errors::Code::kUnexpectedToken, "expected ',' or ']' in array")
            );
        }
    }

    cd::core::Result<std::string> parse_string()
    {
        if (pos_ >= text_.size() || text_[pos_] != '"')
            return std::unexpected(json_errors::make(json_errors::Code::kUnexpectedToken, "expected '\"'"));
        ++pos_;
        std::string out;
        while (pos_ < text_.size())
        {
            const char c = text_[pos_];
            if (c == '"')
            {
                ++pos_;
                return out;
            }
            if (c == '\\')
            {
                ++pos_;
                if (pos_ >= text_.size())
                    return std::unexpected(json_errors::make(json_errors::Code::kBadEscape, "lone backslash"));
                const char e = text_[pos_++];
                switch (e)
                {
                    case '"':
                        out += '"';
                        break;
                    case '\\':
                        out += '\\';
                        break;
                    case '/':
                        out += '/';
                        break;
                    case 'b':
                        out += '\b';
                        break;
                    case 'f':
                        out += '\f';
                        break;
                    case 'n':
                        out += '\n';
                        break;
                    case 'r':
                        out += '\r';
                        break;
                    case 't':
                        out += '\t';
                        break;
                    case 'u':
                    {
                        if (pos_ + 4 > text_.size())
                            return std::unexpected(json_errors::make(json_errors::Code::kBadEscape, "\\u truncated"));
                        std::uint32_t cp = 0;
                        for (int i = 0; i < 4; ++i)
                        {
                            const char h = text_[pos_ + static_cast<std::size_t>(i)];
                            cp <<= 4;
                            if (h >= '0' && h <= '9')
                                cp |= static_cast<std::uint32_t>(h - '0');
                            else if (h >= 'a' && h <= 'f')
                                cp |= static_cast<std::uint32_t>(h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F')
                                cp |= static_cast<std::uint32_t>(h - 'A' + 10);
                            else
                                return std::unexpected(json_errors::make(json_errors::Code::kBadEscape, "\\u non-hex"));
                        }
                        pos_ += 4;
                        // Encode codepoint as UTF-8. (Surrogate pairs not joined; for
                        // anything outside BMP the caller must use a longer-format
                        // input. RFC 8259 allows this trade-off in v1.)
                        if (cp < 0x80u)
                        {
                            out += static_cast<char>(cp);
                        }
                        else if (cp < 0x800u)
                        {
                            out += static_cast<char>(0xC0u | (cp >> 6));
                            out += static_cast<char>(0x80u | (cp & 0x3Fu));
                        }
                        else
                        {
                            out += static_cast<char>(0xE0u | (cp >> 12));
                            out += static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
                            out += static_cast<char>(0x80u | (cp & 0x3Fu));
                        }
                        break;
                    }
                    default:
                        return std::unexpected(
                            json_errors::make(json_errors::Code::kBadEscape, std::string { "unknown escape \\" } + e)
                        );
                }
                continue;
            }
            // Reject raw control chars per RFC 8259 §7.
            if (static_cast<unsigned char>(c) < 0x20u)
                return std::unexpected(
                    json_errors::make(json_errors::Code::kUnterminatedString, "raw control character in string")
                );
            out += c;
            ++pos_;
        }
        return std::unexpected(json_errors::make(json_errors::Code::kUnterminatedString, "EOF in string"));
    }

    cd::core::Result<Value> parse_bool()
    {
        if (text_.compare(pos_, 4, "true") == 0)
        {
            pos_ += 4;
            return Value { true };
        }
        if (text_.compare(pos_, 5, "false") == 0)
        {
            pos_ += 5;
            return Value { false };
        }
        return std::unexpected(json_errors::make(json_errors::Code::kUnexpectedToken, "expected 'true' or 'false'"));
    }

    cd::core::Result<Value> parse_null()
    {
        if (text_.compare(pos_, 4, "null") == 0)
        {
            pos_ += 4;
            return Value { Null {} };
        }
        return std::unexpected(json_errors::make(json_errors::Code::kUnexpectedToken, "expected 'null'"));
    }

    cd::core::Result<Value> parse_number()
    {
        // Find the longest run that looks like a JSON number, then hand it
        // to std::from_chars for IEEE-754 parsing.
        const std::size_t start = pos_;
        if (pos_ < text_.size() && text_[pos_] == '-')
            ++pos_;
        while (pos_ < text_.size())
        {
            const char c = text_[pos_];
            if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-')
                ++pos_;
            else
                break;
        }
        const std::string_view tok = text_.substr(start, pos_ - start);
        double d = 0.0;
        const char* first = tok.data();
        const char* last = first + tok.size();
        const auto r = std::from_chars(first, last, d);
        if (r.ec != std::errc {} || r.ptr != last)
            return std::unexpected(json_errors::make(json_errors::Code::kBadNumber, tok));
        return Value { d };
    }

    void skip_ws() noexcept
    {
        while (pos_ < text_.size())
        {
            const char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                ++pos_;
            else
                break;
        }
    }

    std::string_view text_;
    std::size_t pos_ { 0 };
    std::uint32_t max_depth_ { 64 };
};

void emit(std::string& out, const Value& v, bool pretty, int indent);

void emit_string(std::string& out, const std::string& s)
{
    out += '"';
    for (char c : s)
    {
        switch (c)
        {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20u)
                {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04X", static_cast<unsigned>(c) & 0xFFu);
                    out += buf;
                }
                else
                {
                    out += c;
                }
                break;
        }
    }
    out += '"';
}

void emit_indent(std::string& out, bool pretty, int indent)
{
    if (!pretty)
        return;
    out += '\n';
    for (int i = 0; i < indent; ++i)
        out += "  ";
}

void emit(std::string& out, const Value& v, bool pretty, int indent)
{
    if (v.is_null())
    {
        out += "null";
        return;
    }
    if (v.is_bool())
    {
        out += v.as_bool() ? "true" : "false";
        return;
    }
    if (v.is_number())
    {
        char buf[64];
        const double d = v.as_number();
        // Integer fast-path: avoid '.0' clutter when the number is integral
        // and fits within int64.
        if (d == static_cast<double>(static_cast<std::int64_t>(d)) && d >= -9.0e15 && d <= 9.0e15)
        {
            std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(d));
        }
        else
        {
            std::snprintf(buf, sizeof(buf), "%.17g", d);
        }
        out += buf;
        return;
    }
    if (v.is_string())
    {
        emit_string(out, v.as_string());
        return;
    }
    if (v.is_array())
    {
        const auto& a = v.as_array();
        if (a.empty())
        {
            out += "[]";
            return;
        }
        out += '[';
        bool first = true;
        for (const auto& e : a)
        {
            if (!first)
                out += ',';
            emit_indent(out, pretty, indent + 1);
            emit(out, e, pretty, indent + 1);
            first = false;
        }
        emit_indent(out, pretty, indent);
        out += ']';
        return;
    }
    if (v.is_object())
    {
        const auto& o = v.as_object();
        if (o.empty())
        {
            out += "{}";
            return;
        }
        out += '{';
        bool first = true;
        for (const auto& [k, val] : o)
        {
            if (!first)
                out += ',';
            emit_indent(out, pretty, indent + 1);
            emit_string(out, k);
            out += pretty ? ": " : ":";
            emit(out, val, pretty, indent + 1);
            first = false;
        }
        emit_indent(out, pretty, indent);
        out += '}';
        return;
    }
}

}  // namespace

cd::core::Result<Value> parse(std::string_view text, std::uint32_t max_depth)
{
    Parser p { text, max_depth };
    return p.parse_root();
}

cd::core::Result<Value> load(std::string_view path, std::uint32_t max_depth)
{
    const std::string path_s { path };
    std::ifstream f { path_s, std::ios::binary | std::ios::ate };
    if (!f)
        return std::unexpected(json_errors::make(json_errors::Code::kFileNotFound, path_s));
    const auto size = static_cast<std::size_t>(f.tellg());
    f.seekg(0, std::ios::beg);
    std::string buf;
    buf.resize(size);
    if (size > 0 && !f.read(buf.data(), static_cast<std::streamsize>(size)))
        return std::unexpected(json_errors::make(json_errors::Code::kIoError, path_s));
    return parse(buf, max_depth);
}

std::string serialize(const Value& v, bool pretty)
{
    std::string out;
    emit(out, v, pretty, 0);
    return out;
}

}  // namespace cd::asset_json
