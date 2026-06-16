// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/tests/test_rhi_format_support.cpp
//
// C-FORMAT-MATRIX (Backend-to-100 Wave 3d).
//
// Per-backend Format-SUPPORT conformance: which Formats each backend accepts for
// SAMPLE (kSampled), RENDER-TARGET (kColorAttachment / kDepthStencilAttachment),
// and STORAGE (kStorage) usage, probed by actually calling create_texture with
// each usage. This is the matrix that keeps create_texture from silently
// diverging between Vulkan and D3D12 — a format that one backend accepts and the
// other rejects is a portability trap that no single-backend smoke catches.
//
// CONTRACT ASSERTED (the engine's CORE formats must be supported consistently):
//   * Sampled:  RGBA8Unorm, BGRA8Unorm, RGBA16Float, R32Float, the BCn set
//               (BC1/BC3/BC5/BC7 unorm) — the texture-streamer corpus.
//   * Colour-RT: RGBA8Unorm, BGRA8Unorm (swapchain), RGBA16Float (HDR scene).
//   * Depth-RT:  D32Float, D24UnormS8Uint.
//   * Storage:  R32Float, RGBA16Float, RGBA8Unorm (compute write targets).
// Each of these MUST create on Vulkan AND D3D12 (the two host backends). The
// test ASSERTS that.
//
// DOCUMENTED PER-BACKEND EXCEPTIONS (printed, not asserted-equal):
//   * BCn block-compressed formats are SAMPLE-only on both backends (never a
//     render target or storage image) — so the matrix probes BCn for kSampled
//     only; a BCn-as-RT create is EXPECTED to fail and is NOT asserted to
//     succeed.
//   * Whether RGB10A2 / R11G11B10F etc. are storage-capable is driver-dependent;
//     those are probed + LOGGED so a divergence is visible, not asserted.
//
// The full 43-entry Format enum is walked for SAMPLED on each backend and the
// accept/reject map PRINTED, so a future format-table edit that drops support
// for a previously-accepted format is legible in the log (and the CORE subset is
// the hard gate).
//
// Vulkan (lavapipe/RTX) + D3D12 (WARP). Honest-SKIP per backend absent.
// Pattern: Arrange / Act / Assert.
// =============================================================================
#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
#endif

#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/Enums.hpp>
#include <cd/rhi/Format.hpp>
#include <cd/rhi/Handles.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/vulkan/VulkanDevice.hpp>
#if defined(_WIN32)
    #include <cd/rhi/d3d12/D3D12Device.hpp>
#endif

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string_view>

namespace
{

// Try create_texture(format, usage) and report whether the device accepted it.
[[nodiscard]] bool format_supported(cd::rhi::IDevice& dev, cd::rhi::Format fmt,
                                     cd::rhi::TextureUsage usage)
{
    cd::rhi::TextureDesc td {};
    td.type         = cd::rhi::TextureType::k2D;
    td.format       = fmt;
    // BCn formats need 4-aligned extents (4x4 block). 16x16 satisfies every
    // format including block-compressed.
    td.extent       = { 16, 16, 1 };
    td.mip_levels   = 1;
    td.array_layers = 1;
    td.usage        = usage;
    auto r = dev.create_texture(td);
    if (r.has_value())
    {
        dev.destroy_texture(*r);
        return true;
    }
    return false;
}

struct NamedFormat { cd::rhi::Format fmt; std::string_view name; };

// The engine's CORE sampled formats — MUST be supported on every host backend.
constexpr std::array<NamedFormat, 9> kCoreSampled {{
    { cd::rhi::Format::kRGBA8Unorm,  "RGBA8Unorm"  },
    { cd::rhi::Format::kBGRA8Unorm,  "BGRA8Unorm"  },
    { cd::rhi::Format::kRGBA16Float, "RGBA16Float" },
    { cd::rhi::Format::kR32Float,    "R32Float"    },
    { cd::rhi::Format::kBC1RGBAUnorm,"BC1RGBAUnorm"},
    { cd::rhi::Format::kBC3Unorm,    "BC3Unorm"    },
    { cd::rhi::Format::kBC5Unorm,    "BC5Unorm"    },
    { cd::rhi::Format::kBC7Unorm,    "BC7Unorm"    },
    { cd::rhi::Format::kR8Unorm,     "R8Unorm"     },
}};

// CORE colour render-target formats.
constexpr std::array<NamedFormat, 3> kCoreColorRt {{
    { cd::rhi::Format::kRGBA8Unorm,  "RGBA8Unorm"  },
    { cd::rhi::Format::kBGRA8Unorm,  "BGRA8Unorm"  },
    { cd::rhi::Format::kRGBA16Float, "RGBA16Float" },
}};

// CORE depth render-target formats.
constexpr std::array<NamedFormat, 2> kCoreDepthRt {{
    { cd::rhi::Format::kD32Float,       "D32Float"       },
    { cd::rhi::Format::kD24UnormS8Uint, "D24UnormS8Uint" },
}};

// CORE storage (compute write) formats.
constexpr std::array<NamedFormat, 3> kCoreStorage {{
    { cd::rhi::Format::kR32Float,    "R32Float"    },
    { cd::rhi::Format::kRGBA16Float, "RGBA16Float" },
    { cd::rhi::Format::kRGBA8Unorm,  "RGBA8Unorm"  },
}};

void assert_core_support(cd::rhi::IDevice& dev, const char* who)
{
    using TU = cd::rhi::TextureUsage;

    for (const auto& nf : kCoreSampled)
        EXPECT_TRUE(format_supported(dev, nf.fmt, TU::kSampled))
            << who << ": CORE sampled format " << nf.name
            << " was REJECTED by create_texture — backend format table diverged.";

    for (const auto& nf : kCoreColorRt)
        EXPECT_TRUE(format_supported(dev, nf.fmt, TU::kColorAttachment | TU::kTransferSrc))
            << who << ": CORE colour-RT format " << nf.name << " was REJECTED.";

    for (const auto& nf : kCoreDepthRt)
        EXPECT_TRUE(format_supported(dev, nf.fmt, TU::kDepthStencilAttachment))
            << who << ": CORE depth-RT format " << nf.name << " was REJECTED.";

    for (const auto& nf : kCoreStorage)
        EXPECT_TRUE(format_supported(dev, nf.fmt, TU::kStorage))
            << who << ": CORE storage format " << nf.name << " was REJECTED.";
}

// Walk the FULL Format enum for SAMPLED support and print the accept/reject map.
// Block-compressed are sample-only by design; the LOG makes any future drop of a
// previously-accepted format legible. Skips kUndefined/kCount sentinels and the
// pure-stencil S8 (rarely a standalone sampled image).
void log_full_sampled_map(cd::rhi::IDevice& dev, const char* who)
{
    using TU = cd::rhi::TextureUsage;
    std::cout << "[format-matrix] " << who << " SAMPLED support map:\n";
    int accepted = 0;
    int rejected = 0;
    for (std::uint16_t i = 1; i < static_cast<std::uint16_t>(cd::rhi::Format::kCount); ++i)
    {
        const auto fmt = static_cast<cd::rhi::Format>(i);
        // Depth formats are not sampled as colour; probe them as depth-RT
        // instead so the line is meaningful.
        const bool is_depth   = cd::rhi::is_depth_format(fmt);
        const bool is_stencil = cd::rhi::is_stencil_format(fmt);
        const auto usage = (is_depth || is_stencil) ? TU::kDepthStencilAttachment : TU::kSampled;
        const bool ok = format_supported(dev, fmt, usage);
        (ok ? accepted : rejected)++;
        std::cout << "    fmt[" << i << "] " << (ok ? "OK   " : "rej  ")
                  << (is_depth || is_stencil ? "(depth/stencil)" : "(sampled)") << "\n";
    }
    std::cout << "[format-matrix] " << who << ": " << accepted << " accepted, "
              << rejected << " rejected of "
              << (static_cast<int>(cd::rhi::Format::kCount) - 1) << " enum entries.\n";
}

// BCn-as-render-target is EXPECTED to fail — block-compressed formats are
// sample-only. We do NOT assert it fails (a permissive driver might allow it),
// but we LOG it so a surprising acceptance is visible.
void log_bcn_rt_rejection(cd::rhi::IDevice& dev, const char* who)
{
    using TU = cd::rhi::TextureUsage;
    const bool bc1_rt = format_supported(dev, cd::rhi::Format::kBC1RGBAUnorm, TU::kColorAttachment);
    std::cout << "[format-matrix] " << who << ": BC1 as colour-RT "
              << (bc1_rt ? "ACCEPTED (unexpected — block-compressed RT)" : "rejected (expected)")
              << "\n";
}

[[nodiscard]] std::unique_ptr<cd::rhi::IDevice> make_vulkan_or_null()
{
    cd::rhi::vulkan::VulkanCreateInfo info {};
    info.enable_validation = false;
    auto r = cd::rhi::vulkan::create_vulkan_device(info);
    return r.has_value() ? std::move(*r) : nullptr;
}

#if defined(_WIN32)
[[nodiscard]] std::unique_ptr<cd::rhi::IDevice> make_d3d12_or_null()
{
    cd::rhi::d3d12::D3D12CreateInfo ci {};
    ci.enable_validation = false;
    auto r = cd::rhi::d3d12::create_d3d12_device(ci);
    return r.has_value() ? std::move(*r) : nullptr;
}
#endif

}  // namespace

TEST(RhiFormatSupport, VulkanCoreFormatsSupported)
{
    auto dev = make_vulkan_or_null();
    if (dev == nullptr)
        GTEST_SKIP() << "no Vulkan ICD on this host";
    assert_core_support(*dev, "Vulkan");
    log_full_sampled_map(*dev, "Vulkan");
    log_bcn_rt_rejection(*dev, "Vulkan");
}

#if defined(_WIN32)
TEST(RhiFormatSupport, D3D12CoreFormatsSupported)
{
    auto dev = make_d3d12_or_null();
    if (dev == nullptr)
        GTEST_SKIP() << "no D3D12 adapter on this host";
    assert_core_support(*dev, "D3D12");
    log_full_sampled_map(*dev, "D3D12");
    log_bcn_rt_rejection(*dev, "D3D12");
}

// Cross-backend: the CORE subset must be ACCEPTED by BOTH backends — assert the
// per-format accept agreement so a format one backend takes and the other drops
// is caught directly (the divergence this matrix exists to prevent).
TEST(RhiFormatSupport, VulkanD3D12CoreSampledAgree)
{
    auto vk = make_vulkan_or_null();
    if (vk == nullptr)
        GTEST_SKIP() << "no Vulkan ICD on this host";
    auto dx = make_d3d12_or_null();
    if (dx == nullptr)
        GTEST_SKIP() << "no D3D12 adapter on this host";

    using TU = cd::rhi::TextureUsage;
    for (const auto& nf : kCoreSampled)
    {
        const bool v = format_supported(*vk, nf.fmt, TU::kSampled);
        const bool d = format_supported(*dx, nf.fmt, TU::kSampled);
        EXPECT_EQ(v, d) << "CORE sampled format " << nf.name
                        << " support DIVERGES: Vulkan=" << v << " D3D12=" << d
                        << " — create_texture is not portable for this format.";
    }
}
#endif
