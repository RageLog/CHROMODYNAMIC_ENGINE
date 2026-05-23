// =============================================================================
// CHROMODYNAMIC — engine/foundation/diag/src/Assert.cpp
//
// Process-wide panic handler storage + default impl + dispatch.
// =============================================================================
#include <cd/diag/Assert.hpp>

#include <atomic>
#include <cstdio>
#include <cstdlib>

namespace cd::diag
{

namespace
{

void default_panic_handler(const PanicInfo& info) noexcept
{
    std::fprintf(stderr, "\n=== CHROMODYNAMIC PANIC ===\n");
    std::fprintf(stderr, "  file : %.*s:%u\n",
                 static_cast<int>(info.file.size()), info.file.data(), info.line);
    if (!info.function.empty())
        std::fprintf(stderr, "  fn   : %.*s\n",
                     static_cast<int>(info.function.size()), info.function.data());
    if (!info.expression.empty())
        std::fprintf(stderr, "  expr : %.*s\n",
                     static_cast<int>(info.expression.size()), info.expression.data());
    if (!info.message.empty())
        std::fprintf(stderr, "  msg  : %.*s\n",
                     static_cast<int>(info.message.size()), info.message.data());
    std::fprintf(stderr, "===========================\n");
    std::fflush(stderr);
}

std::atomic<PanicHandler> g_handler { &default_panic_handler };

}  // namespace

PanicHandler set_panic_handler(PanicHandler handler) noexcept
{
    auto fn = handler != nullptr ? handler : &default_panic_handler;
    return g_handler.exchange(fn, std::memory_order_acq_rel);
}

PanicHandler current_panic_handler() noexcept
{
    return g_handler.load(std::memory_order_acquire);
}

PanicHandler reset_panic_handler() noexcept
{
    return g_handler.exchange(&default_panic_handler, std::memory_order_acq_rel);
}

[[noreturn]] void panic(const PanicInfo& info)
{
    auto* h = g_handler.load(std::memory_order_acquire);
    if (h != nullptr)
        h(info);  // may throw — propagate without catching.
    // If the handler chose not to terminate, abort here so a
    // misbehaving handler can't mask an invariant break.
    std::abort();
}

}  // namespace cd::diag
