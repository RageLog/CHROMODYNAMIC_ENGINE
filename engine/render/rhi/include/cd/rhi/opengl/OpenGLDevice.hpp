// =============================================================================
// CHROMODYNAMIC — cd/rhi/opengl/OpenGLDevice.hpp
// Phase 18.B / Wave 178 — OpenGL 4.6 backend.
// Status: intentionally out-of-charter (ADR-20260616 §B-OPENGL, wontfix).
// create_buffer + GL context init implemented; remainder returns kNotImplemented
// by design. Not counted in the 3-backend (Vulkan/D3D12/Metal) parity bar.
// =============================================================================
#pragma once

#include <cd/core/Result.hpp>
#include <cd/rhi/IDevice.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace cd::rhi::opengl
{

struct GLCreateInfo
{
    std::string app_name { "CHROMODYNAMIC" };
    /// Requested minimum GL version (major*10 + minor). 46 = 4.6.
    std::uint32_t min_version { 46 };
    /// Enable the GL debug callback (debug builds only — driver must
    /// support GL_KHR_debug or GL_ARB_debug_output).
    bool enable_validation { true };
    /// When true, the factory creates its own dummy window for context
    /// creation. When false the caller must have a current context
    /// already (offscreen + interop scenarios).
    bool create_dummy_window { true };
};

/// Construct an OpenGL-backed IDevice. Returns kBackendInitFailed
/// when:
///   * No GL driver / loader found (e.g. Windows without an OpenGL
///     ICD, Linux without libGL).
///   * The driver reports a version below `min_version`.
[[nodiscard]] cd::core::Result<std::unique_ptr<cd::rhi::IDevice>>
create_gl_device(GLCreateInfo info = {});

}  // namespace cd::rhi::opengl
