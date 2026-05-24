// =============================================================================
// CHROMODYNAMIC — cd/io/ByteBuffer.hpp
// Phase 73.B / Wave 241 — owned resizable byte buffer with append helpers.
//
// Convenience wrapper over `std::vector<std::byte>` for asset cookers,
// network packet builders, and scratch buffers. Adds:
//   * `append(span)` — bulk-copy bytes.
//   * `append(value)` — trivially-copyable POD append (little-endian on
//                       little-endian targets; not byte-swapped — caller
//                       handles endianness via cd::io::store_le if needed).
//   * `clear`, `data`, `size`, `as_span` getters.
//
// Companion to BinaryWriter (Phase S2.6, schema-style with length-prefix
// strings); ByteBuffer is the lighter no-schema concat builder.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstddef>
#include <cstring>
#include <span>
#include <type_traits>
#include <vector>

namespace cd::io
{

class ByteBuffer
{
public:
    void append(std::span<const std::byte> bytes)
    {
        buf_.insert(buf_.end(), bytes.begin(), bytes.end());
    }

    template <class T>
    void append_pod(const T& value)
    {
        static_assert(std::is_trivially_copyable_v<T>, "ByteBuffer::append_pod requires trivially-copyable T");
        const auto* p = reinterpret_cast<const std::byte*>(&value);
        buf_.insert(buf_.end(), p, p + sizeof(T));
    }

    void clear() noexcept { buf_.clear(); }

    [[nodiscard]] std::size_t size() const noexcept { return buf_.size(); }
    [[nodiscard]] bool empty() const noexcept { return buf_.empty(); }
    [[nodiscard]] const std::byte* data() const noexcept { return buf_.data(); }
    [[nodiscard]] std::byte*       data() noexcept       { return buf_.data(); }

    [[nodiscard]] std::span<const std::byte> as_span() const noexcept
    {
        return std::span<const std::byte> { buf_.data(), buf_.size() };
    }

    [[nodiscard]] std::vector<std::byte> release() noexcept
    {
        return std::move(buf_);
    }

    void reserve(std::size_t n) { buf_.reserve(n); }

private:
    std::vector<std::byte> buf_;
};

}  // namespace cd::io
