// =============================================================================
// CHROMODYNAMIC — engine/render/rhi_metal/src/MetalDevice.cpp
//
// Metal backend skeleton. On non-Apple platforms the factory returns
// kBackendInitFailed. On Apple this TU compiles to the same stub for
// Wave 97; Phase 9 Sprint 1 swaps in the concrete MTLDevice +
// MTLCommandQueue + MTLLibrary wiring.
// =============================================================================
#include <cd/rhi_metal/MetalDevice.hpp>

#include <cd/rhi/IDevice.hpp>

namespace cd::rhi_metal
{

cd::core::Result<std::unique_ptr<cd::rhi::IDevice>>
create_metal_device(MetalCreateInfo /*info*/)
{
#if defined(__APPLE__)
    // Phase 9 Sprint 1 — MTLCreateSystemDefaultDevice + queue + library.
    return std::unexpected(cd::rhi::rhi_errors::make(
        cd::rhi::rhi_errors::Code::kNotImplemented,
        "Metal backend skeleton — Phase 9 Sprint 1 wires the concrete impl"));
#else
    return std::unexpected(cd::rhi::rhi_errors::make(
        cd::rhi::rhi_errors::Code::kBackendInitFailed,
        "Metal backend only available on Apple platforms"));
#endif
}

}  // namespace cd::rhi_metal
