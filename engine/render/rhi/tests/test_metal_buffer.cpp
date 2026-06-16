// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/tests/test_metal_buffer.cpp
//
// Backend-to-100 Wave 4b / C-METAL-TIER2 (docs/METAL_MAC_TESTING.md §3 Tier-2):
//   cd_test_metal_buffer — create_buffer -> lookup -> upload/download (map/
//   unmap) round-trip on the M1 handle registry. A CPU-visible (shared) MTLBuffer
//   written via upload_buffer and read back via download_buffer must round-trip
//   byte-for-byte.
//
// PLATFORM GATE (see test_metal_device.cpp for the full rationale): the body
// instantiates a REAL MTLDevice on Apple (__APPLE__ && CD_RHI_METAL_ENABLED);
// everywhere else a skip-stub registers the case and GTEST_SKIPs cleanly, so the
// file compiles + the target builds + the test registers on Windows and the real
// assertions run only on a Mac.
//
// Pattern: Arrange / Act / Assert. Deterministic; no sleep_for.
// =============================================================================
#if defined(__APPLE__) && defined(CD_RHI_METAL_ENABLED)

    #include <cd/rhi/Descriptors.hpp>
    #include <cd/rhi/Enums.hpp>
    #include <cd/rhi/Handles.hpp>
    #include <cd/rhi/IDevice.hpp>
    #include <cd/rhi/metal/MetalDevice.hpp>

    #include <gtest/gtest.h>

    #include <array>
    #include <cstddef>
    #include <cstdint>
    #include <memory>
    #include <span>
    #include <string>

namespace
{

[[nodiscard]] std::unique_ptr<cd::rhi::IDevice> make_metal_device_or_null()
{
    cd::rhi::metal::MetalCreateInfo ci {};
    ci.enable_validation = false;
    auto r = cd::rhi::metal::create_metal_device(ci);
    return r.has_value() ? std::move(*r) : nullptr;
}

// ---- M1: a shared (CPU-visible) buffer round-trips through map/unmap --------
TEST(MetalBuffer, UploadDownloadRoundTrip)
{
    auto dev = make_metal_device_or_null();
    if (dev == nullptr)
        GTEST_SKIP() << "no Metal device on this Mac";
    auto& d = *dev;

    // Arrange: a CPU-to-GPU (shared) buffer.
    constexpr std::array<std::uint32_t, 4> kSrc { 0xDEADBEEFu, 0x01234567u,
                                                  0x89ABCDEFu, 0xFEEDFACEu };
    cd::rhi::BufferDesc bd {};
    bd.size   = sizeof(kSrc);
    bd.usage  = cd::rhi::BufferUsage::kTransferSrc | cd::rhi::BufferUsage::kUniform;
    bd.memory = cd::rhi::MemoryUsage::kCpuToGpu;
    auto buf_r = d.create_buffer(bd);
    ASSERT_TRUE(buf_r.has_value())
        << "create_buffer must succeed on a real MTLDevice";
    const auto buf = *buf_r;
    ASSERT_TRUE(buf.is_valid());

    // Act: map (upload) then map-read (download).
    const auto up = d.upload_buffer(
        buf, 0,
        std::as_bytes(std::span<const std::uint32_t>(kSrc.data(), kSrc.size())));
    ASSERT_TRUE(up.has_value()) << "upload_buffer (map/unmap) must succeed";

    std::array<std::uint32_t, 4> dst {};
    const auto dl = d.download_buffer(
        buf, 0, std::as_writable_bytes(std::span<std::uint32_t>(dst.data(), dst.size())));
    ASSERT_TRUE(dl.has_value()) << "download_buffer (map) must succeed";

    // Assert: byte-for-byte round-trip.
    EXPECT_EQ(dst, kSrc) << "shared MTLBuffer must round-trip the uploaded bytes";

    d.destroy_buffer(buf);
}

// A GPU-only buffer is not host-mappable; upload_buffer must reject it rather
// than silently corrupting memory (negative / edge case).
TEST(MetalBuffer, UploadToGpuOnlyBufferIsRejected)
{
    auto dev = make_metal_device_or_null();
    if (dev == nullptr)
        GTEST_SKIP() << "no Metal device on this Mac";
    auto& d = *dev;

    cd::rhi::BufferDesc bd {};
    bd.size   = 64;
    bd.usage  = cd::rhi::BufferUsage::kStorage;
    bd.memory = cd::rhi::MemoryUsage::kGpuOnly;
    auto buf_r = d.create_buffer(bd);
    ASSERT_TRUE(buf_r.has_value());
    const auto buf = *buf_r;

    const std::array<std::byte, 64> zeros {};
    const auto up = d.upload_buffer(buf, 0, std::span<const std::byte>(zeros));
    EXPECT_FALSE(up.has_value())
        << "upload_buffer to a GPU-only (private) MTLBuffer must be rejected";

    d.destroy_buffer(buf);
}

}  // namespace

#else  // not (Apple && CD_RHI_METAL_ENABLED)

    #include <gtest/gtest.h>

TEST(MetalBuffer, SkippedOffApple)
{
    GTEST_SKIP() << "Metal backend disabled on this platform (Apple-only)";
}

#endif  // __APPLE__ && CD_RHI_METAL_ENABLED
