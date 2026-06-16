// =============================================================================
// CHROMODYNAMIC — engine/render/rhi_metal/src/MetalDevice.cpp
//
// Plain-C++ Metal factory fallback. The real Apple-Clang device factory lives
// in MetalDevice.mm (compiled only when CD_RHI_METAL_ENABLED is ON, i.e. on
// Apple). This .cpp is the non-Obj-C++ TU that always compiles; it provides
// the kBackendInitFailed factory used when the Metal backend is unavailable on
// the current toolchain/platform.
// =============================================================================
#include <cd/rhi/metal/MetalDevice.hpp>

#include <cd/rhi/IDevice.hpp>

namespace cd::rhi::metal
{

cd::core::Result<std::unique_ptr<cd::rhi::IDevice>>
create_metal_device(MetalCreateInfo /*info*/)
{
#if defined(__APPLE__)
    // Dead branch: when CD_RHI_METAL_ENABLED is ON (the only way this TU sees
    // an Apple toolchain with a real device), the CMake build compiles
    // MetalDevice.mm INSTEAD of this .cpp — so this .cpp factory is the
    // unavailable-backend fallback on every path that actually compiles it.
    // kBackendInitFailed (not kNotImplemented): the backend is genuinely
    // unavailable on this toolchain config, it is not a missing capability.
    return std::unexpected(cd::rhi::rhi_errors::make(
        cd::rhi::rhi_errors::Code::kBackendInitFailed,
        "Metal backend not built in this configuration "
        "(CD_RHI_METAL_ENABLED=OFF) — enable it on an Apple toolchain"));
#else
    return std::unexpected(cd::rhi::rhi_errors::make(
        cd::rhi::rhi_errors::Code::kBackendInitFailed,
        "Metal backend only available on Apple platforms"));
#endif
}

}  // namespace cd::rhi::metal
