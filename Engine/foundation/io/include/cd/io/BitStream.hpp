// =============================================================================
// CHROMODYNAMIC — cd/io/BitStream.hpp
// ADR-017 P3 (Sprint S2.6) — bit-level packed stream primitives.
//
// BitWriter: variable-width unsigned integers (1..64 bits) packed LSB-first
//            into a std::vector<std::byte> buffer.
// BitReader: counterpart consumer over std::span<const std::byte>.
//
// Use cases (later sprints):
//   * Network snapshot quantization (rotation as ⟨14b yaw⟩+⟨14b pitch⟩+…)
//   * Compressed input replay logs
//   * Mesh index streams with varint widths
//
// Conventions:
//   - The producer must commit the trailing partial byte via `finish()` before
//     reading the buffer; `data()` includes any in-flight partial byte.
//   - Reader EOF behaviour: returns ErrorCode (binary_errors::kEndOfStream).
// =============================================================================
#pragma once

#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>
#include <cd/io/BinaryStream.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace cd::io
{

class BitWriter
{
public:
    BitWriter() = default;

    explicit BitWriter(std::size_t reserve_bytes)
    {
        buffer_.reserve(reserve_bytes);
    }

    /// Write `bits` low-order bits of `value`, LSB-first. `bits` must be in [1, 64].
    void write_bits(std::uint64_t value, std::uint32_t bits) noexcept
    {
        if (bits == 0)
            return;
        if (bits > 64)
            bits = 64;
        // Mask off any high bits the caller might have left lit.
        if (bits < 64)
            value &= (std::uint64_t { 1 } << bits) - 1;

        pending_ |= (value << pending_bits_);
        pending_bits_ += bits;
        while (pending_bits_ >= 8)
        {
            buffer_.push_back(static_cast<std::byte>(pending_ & 0xFFu));
            pending_ >>= 8;
            pending_bits_ -= 8;
        }
    }

    void write_bit(bool b) noexcept
    {
        write_bits(b ? 1u : 0u, 1);
    }

    /// Flush any pending partial byte to the buffer (zero-pad on the high bits).
    void finish() noexcept
    {
        if (pending_bits_ > 0)
        {
            buffer_.push_back(static_cast<std::byte>(pending_ & 0xFFu));
            pending_ = 0;
            pending_bits_ = 0;
        }
    }

    [[nodiscard]] std::span<const std::byte> data() noexcept
    {
        finish();
        return { buffer_.data(), buffer_.size() };
    }

    [[nodiscard]] std::size_t bit_position() const noexcept
    {
        return buffer_.size() * 8 + pending_bits_;
    }

    [[nodiscard]] std::vector<std::byte> release() noexcept
    {
        finish();
        return std::move(buffer_);
    }

    void clear() noexcept
    {
        buffer_.clear();
        pending_ = 0;
        pending_bits_ = 0;
    }

private:
    std::vector<std::byte> buffer_;
    std::uint64_t pending_ { 0 };
    std::uint32_t pending_bits_ { 0 };
};

class BitReader
{
public:
    explicit BitReader(std::span<const std::byte> data) noexcept
        : data_ { data }
    {
    }

    [[nodiscard]] std::size_t bit_position() const noexcept
    {
        return bit_pos_;
    }

    [[nodiscard]] std::size_t bits_remaining() const noexcept
    {
        return data_.size() * 8 - bit_pos_;
    }

    [[nodiscard]] bool at_end() const noexcept
    {
        return bit_pos_ >= data_.size() * 8;
    }

    /// Read `bits` bits LSB-first as an unsigned integer. `bits` in [1, 64].
    cd::core::Result<std::uint64_t> read_bits(std::uint32_t bits) noexcept
    {
        if (bits == 0)
            return std::uint64_t { 0 };
        if (bits > 64)
            bits = 64;
        if (bits_remaining() < bits)
        {
            return std::unexpected(binary_errors::make(binary_errors::Code::kEndOfStream, "bit read past end"));
        }
        std::uint64_t result = 0;
        std::uint32_t produced = 0;
        while (produced < bits)
        {
            const auto byte_idx = bit_pos_ / 8;
            const auto bit_in_byte = bit_pos_ % 8;
            const auto avail = 8 - bit_in_byte;
            const auto take = (bits - produced) < avail ? (bits - produced) : avail;
            const auto src = static_cast<std::uint8_t>(data_[byte_idx]);
            const auto chunk = static_cast<std::uint64_t>((src >> bit_in_byte) & ((1u << take) - 1u));
            result |= chunk << produced;
            produced += static_cast<std::uint32_t>(take);
            bit_pos_ += take;
        }
        return result;
    }

    cd::core::Result<bool> read_bit() noexcept
    {
        auto r = read_bits(1);
        if (!r.has_value())
            return std::unexpected(r.error());
        return *r != 0;
    }

    void seek_bits(std::size_t pos) noexcept
    {
        const auto max_bits = data_.size() * 8;
        bit_pos_ = pos > max_bits ? max_bits : pos;
    }

private:
    std::span<const std::byte> data_;
    std::size_t bit_pos_ { 0 };
};

}  // namespace cd::io
