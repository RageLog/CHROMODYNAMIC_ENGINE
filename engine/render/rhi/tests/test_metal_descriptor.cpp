// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/tests/test_metal_descriptor.cpp
//
// Backend-to-100 Wave 4b / C-METAL-TIER2 (docs/METAL_MAC_TESTING.md §3 Tier-2):
//   cd_test_metal_descriptor — create_descriptor_set_layout ->
//   allocate_descriptor_set -> update_descriptor_set -> bind_descriptor_set
//   argument-buffer round-trip (M4 + the §5.1 [[buffer(N)]] / [[id(binding)]]
//   map). A uniform buffer written into a set's argument buffer and bound to a
//   command buffer must record without a Metal argument-encoder slot conflict.
//
// PLATFORM GATE (see test_metal_device.cpp for the full rationale): real
// MTLDevice on Apple; skip-stub everywhere else. Compiles + registers on
// Windows, runs on a Mac.
//
// Pattern: Arrange / Act / Assert. Deterministic; no sleep_for.
// =============================================================================
#if defined(__APPLE__) && defined(CD_RHI_METAL_ENABLED)

    #include <cd/rhi/Descriptors.hpp>
    #include <cd/rhi/Enums.hpp>
    #include <cd/rhi/Handles.hpp>
    #include <cd/rhi/IDevice.hpp>
    #include <cd/rhi/Pipeline.hpp>
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

// ---- M4: argument-buffer allocate -> update -> bind round-trip --------------
TEST(MetalDescriptor, AllocateUpdateBindUniformBuffer)
{
    auto dev = make_metal_device_or_null();
    if (dev == nullptr)
        GTEST_SKIP() << "no Metal device on this Mac";
    auto& d = *dev;

    // Arrange: a set-0 layout with one uniform buffer at binding 0.
    const std::array<cd::rhi::DescriptorSetLayoutBinding, 1> bindings {
        cd::rhi::DescriptorSetLayoutBinding {
            .binding = 0,
            .type    = cd::rhi::DescriptorType::kUniformBuffer,
            .count   = 1,
            .stages  = cd::rhi::ShaderStage::kAllGraphics }
    };
    cd::rhi::DescriptorSetLayoutDesc ld {};
    ld.bindings = bindings;
    auto layout_r = d.create_descriptor_set_layout(ld);
    ASSERT_TRUE(layout_r.has_value())
        << "create_descriptor_set_layout must succeed on a real MTLDevice";
    const auto layout = *layout_r;

    auto set_r = d.allocate_descriptor_set(layout);
    ASSERT_TRUE(set_r.has_value())
        << "allocate_descriptor_set must back the set with an argument buffer";
    const auto set = *set_r;

    // A backing uniform buffer to write into the set.
    cd::rhi::BufferDesc bd {};
    bd.size   = 64;
    bd.usage  = cd::rhi::BufferUsage::kUniform;
    bd.memory = cd::rhi::MemoryUsage::kCpuToGpu;
    auto buf_r = d.create_buffer(bd);
    ASSERT_TRUE(buf_r.has_value());
    const auto buf = *buf_r;

    // Act: update the argument buffer with the uniform write.
    cd::rhi::DescriptorWrite w {};
    w.binding      = 0;
    w.type         = cd::rhi::DescriptorType::kUniformBuffer;
    w.buffer       = buf;
    w.buffer_range = 64;
    const auto up = d.update_descriptor_set(
        set, std::span<const cd::rhi::DescriptorWrite>(&w, 1));
    EXPECT_TRUE(up.has_value())
        << "update_descriptor_set must encode the buffer into the argument buffer: "
        << (up.has_value() ? std::string {}
                           : std::string(up.error().message.begin(),
                                         up.error().message.end()));

    // Bind it on a command buffer — proves the set survives the encoder path
    // (an argument-buffer slot conflict would surface here under MTL validation).
    auto cmd = d.create_command_buffer(cd::rhi::QueueType::kGraphics);
    ASSERT_NE(cmd, nullptr);
    cmd->begin();
    cmd->bind_descriptor_set(0, set);
    cmd->end();

    d.destroy_buffer(buf);
    d.destroy_descriptor_set(set);
    d.destroy_descriptor_set_layout(layout);
}

// An update naming an unknown buffer handle must be rejected (negative case).
TEST(MetalDescriptor, UpdateWithUnknownHandleRejected)
{
    auto dev = make_metal_device_or_null();
    if (dev == nullptr)
        GTEST_SKIP() << "no Metal device on this Mac";
    auto& d = *dev;

    const std::array<cd::rhi::DescriptorSetLayoutBinding, 1> bindings {
        cd::rhi::DescriptorSetLayoutBinding {
            .binding = 0, .type = cd::rhi::DescriptorType::kUniformBuffer,
            .count = 1, .stages = cd::rhi::ShaderStage::kAllGraphics }
    };
    cd::rhi::DescriptorSetLayoutDesc ld {};
    ld.bindings = bindings;
    auto layout_r = d.create_descriptor_set_layout(ld);
    ASSERT_TRUE(layout_r.has_value());
    const auto layout = *layout_r;
    auto set_r = d.allocate_descriptor_set(layout);
    ASSERT_TRUE(set_r.has_value());
    const auto set = *set_r;

    cd::rhi::DescriptorWrite w {};
    w.binding = 0;
    w.type    = cd::rhi::DescriptorType::kUniformBuffer;
    w.buffer  = cd::rhi::BufferHandle {};  // never created -> unknown
    const auto up = d.update_descriptor_set(
        set, std::span<const cd::rhi::DescriptorWrite>(&w, 1));
    EXPECT_FALSE(up.has_value())
        << "update_descriptor_set must reject an unknown buffer handle";

    d.destroy_descriptor_set(set);
    d.destroy_descriptor_set_layout(layout);
}

}  // namespace

#else  // not (Apple && CD_RHI_METAL_ENABLED)

    #include <gtest/gtest.h>

TEST(MetalDescriptor, SkippedOffApple)
{
    GTEST_SKIP() << "Metal backend disabled on this platform (Apple-only)";
}

#endif  // __APPLE__ && CD_RHI_METAL_ENABLED
