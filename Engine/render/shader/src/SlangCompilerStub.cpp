// =============================================================================
// CHROMODYNAMIC — cd/shader/SlangCompilerStub.cpp
//
// Stub for the Slang backend. Always compiled — the real Slang integration
// (FetchContent of github.com/shader-slang/slang + linking against the
// generated slang/slang-rhi static libs) is a focused follow-up that
// requires bringing in Slang's deep build tree. Until that lands the
// factory hands back nullptr; callers fall back to glslang (the reference
// backend) without any code change.
//
// Slang's value vs glslang:
//   * Slang language (modules, generics, automatic differentiation).
//   * Single source → SPIR-V + DXIL + MSL + WGSL emission.
//   * Reflection that lines up cleanly with descriptor binding indices.
//
// Migration path when the real backend lands: this TU is replaced with a
// SlangCompiler class wrapping `slang::createGlobalSession()` +
// `IModule::loadModuleFromSource()` + `link()` + `getTargetCode()`. The
// `make_slang_compiler()` factory signature does NOT change.
// =============================================================================
#include <cd/shader/Compiler.hpp>

#include <memory>

namespace cd::shader
{

std::unique_ptr<ICompiler> make_slang_compiler()
{
    return nullptr;
}

}  // namespace cd::shader
