// =============================================================================
// CHROMODYNAMIC — cd/io/BinaryStream.hpp
// ADR-017 P3 (Sprint S2.6) — binary serialization primitives.
//
// Two complementary types:
//
//   * BinaryWriter — append-only writer over an owned std::vector<std::byte>.
//                    All multi-byte values stored as little-endian (see
//                    cd::io::Endian). Strings are written as
//                    [u32 little-endian length][bytes...].
//   * BinaryReader — sequential reader over a non-owning std::span<const
//                    std::byte>. Out-of-range reads return std::expected error
//                    (cd::core::Error from `binary_errors`) without exception.
//
// Both types are intentionally minimal: no schema, no versioning. Higher-level
// formats (asset bundles, scene save files) layer on top.
// =============================================================================
#pragma once

#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>
#include <cd/io/Endian.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace cd::io
{

namespace binary_errors
{
inline constexpr std::uint32_t kDomain = 0x0006;

enum class Code : std::uint32_t
{
    kOk = 0,
    kEndOfStream = 1,
    kSizeOverflow = 2,
};

[[nodiscard]] inline cd::core::ErrorCode make(Code c, std::string_view msg = {}) noexcept
{
    return cd::core::ErrorCode { kDomain, static_cast<std::uint32_t>(c), msg };
}
}  // namespace binary_errors

class BinaryWriter
{
public:
    BinaryWriter() = default;

    explicit BinaryWriter(std::size_t reserve_bytes)
    {
        buffer_.reserve(reserve_bytes);
    }

    void reserve(std::size_t n)
    {
        buffer_.reserve(n);
    }

    void write_bytes(const std::byte* data, std::size_t n)
    {
        if (n == 0)
            return;
        const auto old = buffer_.size();
        buffer_.resize(old + n);
        std::memcpy(buffer_.data() + old, data, n);
    }

    void write_bytes(std::span<const std::byte> bytes)
    {
        write_bytes(bytes.data(), bytes.size());
    }

    template <class T>
    void write(T value)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        static_assert(!std::is_pointer_v<T>, "Refusing to serialize raw pointer");
        std::byte tmp[sizeof(T)];
        store_le(tmp, value);
        write_bytes(tmp, sizeof(T));
    }

    void write_bool(bool b)
    {
        write<std::uint8_t>(b ? 1 : 0);
    }

    void write_string(std::string_view s)
    {
        write<std::uint32_t>(static_cast<std::uint32_t>(s.size()));
        write_bytes(reinterpret_cast<const std::byte*>(s.data()), s.size());
    }

    [[nodiscard]] std::span<const std::byte> data() const noexcept
    {
        return { buffer_.data(), buffer_.size() };
    }

    [[nodiscard]] const std::vector<std::byte>& buffer() const noexcept
    {
        return buffer_;
    }

    [[nodiscard]] std::vector<std::byte> release() noexcept
    {
        return std::move(buffer_);
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        return buffer_.size();
    }

    void clear() noexcept
    {
        buffer_.clear();
    }

private:
    std::vector<std::byte> buffer_;
};

class BinaryReader
{
public:
    explicit BinaryReader(std::span<const std::byte> data) noexcept
        : data_ { data }
    {
    }

    [[nodiscard]] std::size_t position() const noexcept
    {
        return pos_;
    }

    [[nodiscard]] std::size_t remaining() const noexcept
    {
        return data_.size() - pos_;
    }

    [[nodiscard]] bool at_end() const noexcept
    {
        return pos_ >= data_.size();
    }

    void seek(std::size_t p) noexcept
    {
        pos_ = p > data_.size() ? data_.size() : p;
    }

    /// Non-advancing read: inspect the next sizeof(T) bytes without advancing
    /// the position. Returns kEndOfStream if fewer than sizeof(T) remain.
    template <class T>
    [[nodiscard]] cd::core::Result<T> peek() const
    {
        static_assert(std::is_trivially_copyable_v<T>);
        static_assert(!std::is_pointer_v<T>, "Refusing to deserialize raw pointer");
        if (remaining() < sizeof(T))
        {
            return std::unexpected(binary_errors::make(binary_errors::Code::kEndOfStream, "peek past end"));
        }
        return load_le<T>(data_.data() + pos_);
    }

    cd::core::Result<void> read_bytes(std::byte* dst, std::size_t n)
    {
        if (remaining() < n)
        {
            return std::unexpected(binary_errors::make(binary_errors::Code::kEndOfStream, "read_bytes past end"));
        }
        std::memcpy(dst, data_.data() + pos_, n);
        pos_ += n;
        return {};
    }

    template <class T>
    cd::core::Result<T> read()
    {
        static_assert(std::is_trivially_copyable_v<T>);
        static_assert(!std::is_pointer_v<T>, "Refusing to deserialize raw pointer");
        if (remaining() < sizeof(T))
        {
            return std::unexpected(binary_errors::make(binary_errors::Code::kEndOfStream, "read past end"));
        }
        T v = load_le<T>(data_.data() + pos_);
        pos_ += sizeof(T);
        return v;
    }

    cd::core::Result<bool> read_bool()
    {
        auto r = read<std::uint8_t>();
        if (!r.has_value())
            return std::unexpected(r.error());
        return *r != 0;
    }

    cd::core::Result<std::string> read_string()
    {
        auto len = read<std::uint32_t>();
        if (!len.has_value())
            return std::unexpected(len.error());
        const auto n = static_cast<std::size_t>(*len);
        if (remaining() < n)
        {
            return std::unexpected(binary_errors::make(binary_errors::Code::kEndOfStream, "string body past end"));
        }
        std::string s;
        s.resize(n);
        if (n > 0)
            std::memcpy(s.data(), data_.data() + pos_, n);
        pos_ += n;
        return s;
    }

private:
    std::span<const std::byte> data_;
    std::size_t pos_ { 0 };
};

}  // namespace cd::io
