// =============================================================================
// CHROMODYNAMIC — cd/io/Framing.hpp
// ADR-017 P3 (Sprint S2.6) — length-prefix framing for byte streams.
//
// Producer: LengthPrefixWriter — writes [u32 little-endian length][payload].
// Consumer: LengthPrefixDecoder — incrementally consumes arbitrary byte
//           chunks, emits complete frames as std::vector<std::byte>. Suitable
//           for TCP-style transports, asset bundle chunk parsing, IPC.
//
// Limits:
//   * Per-frame body capped at `max_frame_bytes` (default 16 MiB) — exceeding
//     emits a `binary_errors::Code::kSizeOverflow` and the decoder enters a
//     terminal error state until reset().
// =============================================================================
#pragma once

#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>
#include <cd/io/BinaryStream.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace cd::io
{

class LengthPrefixWriter
{
public:
    /// Append `[u32 little-endian length][bytes]` into the underlying writer.
    static void write_frame(BinaryWriter& w, std::span<const std::byte> bytes)
    {
        w.write<std::uint32_t>(static_cast<std::uint32_t>(bytes.size()));
        w.write_bytes(bytes);
    }
};

class LengthPrefixDecoder
{
public:
    static constexpr std::size_t kDefaultMaxFrame = static_cast<std::size_t>(16) * 1024u * 1024u;  // 16 MiB
    static constexpr std::size_t kHeaderSize = 4u;

    explicit LengthPrefixDecoder(std::size_t max_frame_bytes = kDefaultMaxFrame)
        : max_frame_bytes_ { max_frame_bytes }
    {
    }

    /// Append bytes into the decoder's internal buffer. Call `next_frame()`
    /// repeatedly until it returns std::nullopt to drain ready frames.
    void feed(std::span<const std::byte> chunk)
    {
        if (chunk.empty())
            return;
        const auto old = buffer_.size();
        buffer_.resize(old + chunk.size());
        std::memcpy(buffer_.data() + old, chunk.data(), chunk.size());
    }

    /// Pull one complete frame off the buffer. Returns:
    ///   * payload bytes (move-out) when a full frame is available
    ///   * std::nullopt when the buffer is shorter than the next frame
    ///   * std::unexpected on size-overflow / fatal state
    cd::core::Result<std::optional<std::vector<std::byte>>> next_frame()
    {
        if (errored_)
        {
            return std::unexpected(
                binary_errors::make(binary_errors::Code::kSizeOverflow, "decoder in terminal error state")
            );
        }
        if (buffer_.size() < kHeaderSize)
            return std::nullopt;
        std::uint32_t len = load_le<std::uint32_t>(buffer_.data());
        if (len > max_frame_bytes_)
        {
            errored_ = true;
            return std::unexpected(
                binary_errors::make(binary_errors::Code::kSizeOverflow, "frame exceeds maximum size")
            );
        }
        if (buffer_.size() < kHeaderSize + len)
            return std::nullopt;

        std::vector<std::byte> frame(len);
        if (len > 0)
        {
            std::memcpy(frame.data(), buffer_.data() + kHeaderSize, len);
        }
        // Compact: erase the front of the buffer.
        const auto consumed = kHeaderSize + len;
        if (consumed == buffer_.size())
        {
            buffer_.clear();
        }
        else
        {
            std::memmove(buffer_.data(), buffer_.data() + consumed, buffer_.size() - consumed);
            buffer_.resize(buffer_.size() - consumed);
        }
        return frame;
    }

    void reset() noexcept
    {
        buffer_.clear();
        errored_ = false;
    }

    [[nodiscard]] std::size_t buffered_bytes() const noexcept
    {
        return buffer_.size();
    }

    [[nodiscard]] bool errored() const noexcept
    {
        return errored_;
    }

    [[nodiscard]] std::size_t max_frame_bytes() const noexcept
    {
        return max_frame_bytes_;
    }

private:
    std::vector<std::byte> buffer_;
    std::size_t max_frame_bytes_;
    bool errored_ { false };
};

}  // namespace cd::io
