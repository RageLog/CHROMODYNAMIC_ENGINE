// =============================================================================
// CHROMODYNAMIC — cd/asset_image/Bc7Compress.cpp
// =============================================================================
#include <cd/asset_image/Bc7.hpp>

extern "C" {
#include <bc7enc.h>
}

#include <algorithm>
#include <array>
#include <cstring>
#include <mutex>

namespace cd::asset_image
{

namespace
{

constexpr std::uint32_t kBlockSize = 4;
constexpr std::uint32_t kBytesPerBlock = 16;

/// bc7enc requires a one-shot `bc7enc_compress_block_init()` global init
/// (it precomputes RD tables). std::call_once + a global flag keeps the
/// init thread-safe.
void ensure_bc7_init()
{
    static std::once_flag flag;
    std::call_once(flag, [] { ::bc7enc_compress_block_init(); });
}

[[nodiscard]] bc7enc_compress_block_params make_params(Bc7Quality q) noexcept
{
    bc7enc_compress_block_params p {};
    ::bc7enc_compress_block_params_init(&p);
    switch (q)
    {
        case Bc7Quality::kFast:
            p.m_uber_level = 0;
            p.m_max_partitions_mode = 16;
            break;
        case Bc7Quality::kBalanced:
            p.m_uber_level = 2;
            p.m_max_partitions_mode = 32;
            break;
        case Bc7Quality::kHigh:
            p.m_uber_level = BC7ENC_MAX_UBER_LEVEL;
            // bc7enc spells the partition count as MAX_PARTITIONS1 with
            // a trailing digit; not a typo on our end.
            p.m_max_partitions_mode = BC7ENC_MAX_PARTITIONS1;
            break;
    }
    return p;
}

}  // namespace

cd::core::Result<Bc7Block> compress_bc7(
    std::span<const std::uint8_t> rgba,
    std::uint32_t width,
    std::uint32_t height,
    Bc7Quality quality
)
{
    if (width == 0 || height == 0)
    {
        return std::unexpected(
            bc7_errors::make(bc7_errors::Code::kUnsupportedDimensions, "width/height must be > 0")
        );
    }
    const std::size_t expected_bytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U;
    if (rgba.size() < expected_bytes)
    {
        return std::unexpected(
            bc7_errors::make(bc7_errors::Code::kInvalidArgument, "rgba span smaller than width*height*4")
        );
    }

    ensure_bc7_init();
    const auto params = make_params(quality);

    const std::uint32_t bw = (width + kBlockSize - 1) / kBlockSize;
    const std::uint32_t bh = (height + kBlockSize - 1) / kBlockSize;

    Bc7Block out;
    out.block_w = bw;
    out.block_h = bh;
    out.data.resize(static_cast<std::size_t>(bw) * static_cast<std::size_t>(bh) * kBytesPerBlock);

    // Source pitch in bytes.
    const std::size_t src_pitch = static_cast<std::size_t>(width) * 4U;

    // Scratch 4x4 RGBA block (16 pixels × 4 bytes = 64 bytes) — bc7enc's
    // signature wants a contiguous 16-pixel array per block.
    std::array<std::uint8_t, kBlockSize * kBlockSize * 4> block_pixels {};

    for (std::uint32_t by = 0; by < bh; ++by)
    {
        for (std::uint32_t bx = 0; bx < bw; ++bx)
        {
            // Fill the 4x4 scratch by copying rows of up to 4 pixels each.
            // Pad with zeros where the source ends before a full block.
            block_pixels.fill(0);
            for (std::uint32_t py = 0; py < kBlockSize; ++py)
            {
                const std::uint32_t sy = by * kBlockSize + py;
                if (sy >= height)
                    break;
                for (std::uint32_t px = 0; px < kBlockSize; ++px)
                {
                    const std::uint32_t sx = bx * kBlockSize + px;
                    if (sx >= width)
                        break;
                    const std::size_t src_off = static_cast<std::size_t>(sy) * src_pitch +
                                                static_cast<std::size_t>(sx) * 4U;
                    const std::size_t dst_off = (static_cast<std::size_t>(py) * kBlockSize + px) * 4U;
                    std::memcpy(&block_pixels[dst_off], &rgba[src_off], 4U);
                }
            }

            const std::size_t out_off =
                (static_cast<std::size_t>(by) * bw + static_cast<std::size_t>(bx)) * kBytesPerBlock;
            ::bc7enc_compress_block(out.data.data() + out_off, block_pixels.data(), &params);
        }
    }

    return out;
}

}  // namespace cd::asset_image
