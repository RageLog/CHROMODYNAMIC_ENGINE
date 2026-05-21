// =============================================================================
// CHROMODYNAMIC — samples/hello_core
// Phase 2 Sprint S2.0 — first runnable sample, proves cd::core links + runs
// =============================================================================
#include <cd/core/Defines.hpp>
#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>
#include <cd/core/Version.hpp>

#include <cstdio>
#include <string_view>

namespace
{

cd::core::Result<int> compute_meaning(bool ready)
{
    if (!ready)
        return cd::core::fail(cd::core::core_errors::Code::kAborted, "not ready yet");
    return 42;
}

constexpr std::string_view platform_tag() noexcept
{
#if defined(CD_PLATFORM_WINDOWS)
    return "Windows";
#elif defined(CD_PLATFORM_LINUX)
    return "Linux";
#elif defined(CD_PLATFORM_MACOS)
    return "macOS";
#elif defined(CD_PLATFORM_IOS)
    return "iOS";
#elif defined(CD_PLATFORM_ANDROID)
    return "Android";
#elif defined(CD_PLATFORM_WEB)
    return "Web";
#else
    return "Unknown";
#endif
}

constexpr std::string_view compiler_tag() noexcept
{
#if defined(CD_COMPILER_MSVC)
    return "MSVC";
#elif defined(CD_COMPILER_CLANG_CL)
    return "Clang-CL";
#elif defined(CD_COMPILER_CLANG)
    return "Clang";
#elif defined(CD_COMPILER_GCC)
    return "GCC";
#else
    return "Unknown";
#endif
}

constexpr std::string_view arch_tag() noexcept
{
#if defined(CD_ARCH_X86_64)
    return "x86_64";
#elif defined(CD_ARCH_ARM64)
    return "ARM64";
#elif defined(CD_ARCH_RISCV64)
    return "RISC-V64";
#else
    return "Unknown";
#endif
}

}  // namespace

int main()
{
    std::printf(
        "=== %.*s %u.%u.%u ===\n",
        static_cast<int>(cd::core::kEngineName.size()),
        cd::core::kEngineName.data(),
        static_cast<unsigned>(cd::core::kEngineVersion.major),
        static_cast<unsigned>(cd::core::kEngineVersion.minor),
        static_cast<unsigned>(cd::core::kEngineVersion.patch)
    );
    std::printf("Platform: %.*s\n", static_cast<int>(platform_tag().size()), platform_tag().data());
    std::printf("Compiler: %.*s\n", static_cast<int>(compiler_tag().size()), compiler_tag().data());
    std::printf("Arch    : %.*s\n", static_cast<int>(arch_tag().size()), arch_tag().data());
    std::printf("CacheLn : %llu bytes\n", static_cast<unsigned long long>(cd::core::kCacheLineSize));

    auto r = compute_meaning(true);
    if (!r)
    {
        std::printf(
            "compute_meaning failed (code=%u, %.*s)\n",
            r.error().code,
            static_cast<int>(r.error().message.size()),
            r.error().message.data()
        );
        return 1;
    }
    std::printf("Result  : %d\n", *r);

    auto err = compute_meaning(false);
    if (err)
    {
        std::printf("ERROR: expected failure but got value %d\n", *err);
        return 2;
    }
    std::printf(
        "Error path tested OK (code=%u, %.*s)\n",
        err.error().code,
        static_cast<int>(err.error().message.size()),
        err.error().message.data()
    );

    std::printf("[hello_core] cd::core MVP OK\n");
    return 0;
}
