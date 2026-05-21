// =============================================================================
// CHROMODYNAMIC — cd/shader/GlslangCompilerStub.cpp
//
// Compiled instead of GlslangCompiler.cpp when CD_ENABLE_GLSLANG=OFF. Returns
// nullptr from the factory so callers can branch to a different strategy
// (pre-compiled SPIR-V loaded via `load_spirv_file`) without unresolved-symbol
// link errors.
// =============================================================================
#include <cd/shader/Compiler.hpp>

#include <memory>

namespace cd::shader
{

std::unique_ptr<ICompiler> make_glslang_compiler()
{
    return nullptr;
}

}  // namespace cd::shader
