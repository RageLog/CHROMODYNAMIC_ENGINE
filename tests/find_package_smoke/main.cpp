// =============================================================================
// tests/find_package_smoke/main.cpp
// Phase 10 / Sprint 4 / Wave 121 — downstream find_package() consumer.
//
// Exercises one symbol from each foundation library that the install
// tree is expected to ship: cd::core (ErrorCode + format), cd::diag
// (CD_VERIFY against a known-true expression), cd::log (RingBufferSink).
//
// Exit 0 means the prefix-installed CHROMODYNAMIC is usable from a
// standalone consumer project without rebuilding the engine.
// =============================================================================

#include <cd/core/ErrorCode.hpp>
#include <cd/core/ErrorFormat.hpp>
#include <cd/diag/Assert.hpp>
#include <cd/log/RingBufferSink.hpp>

#include <cstdio>
#include <string>

int main()
{
    // cd::core — construct an ErrorCode and format it.
    const auto ec = cd::core::core_errors::make(
        cd::core::core_errors::Code::kNotFound, "smoke");
    const auto formatted = cd::core::format(ec);
    if (formatted != "core::NotFound: smoke") {
        std::fprintf(stderr, "format() returned %s\n", formatted.c_str());
        return 1;
    }

    // cd::diag — verify a true expression. (Verify on false would panic.)
    CD_VERIFY(1 + 1 == 2);

    // cd::log — push a record into a ring buffer and read it back.
    cd::log::RingBufferSink sink { 4 };
    cd::log::LogRecord r;
    r.message = "find_package smoke";
    sink.on_log_record(r);
    const auto snap = sink.snapshot();
    if (snap.size() != 1 || snap[0].message != "find_package smoke") {
        std::fprintf(stderr, "RingBufferSink snapshot mismatch\n");
        return 1;
    }

    std::fprintf(stdout,
                 "find_package(CHROMODYNAMIC) smoke OK — formatted=%s, "
                 "ring size=%zu\n",
                 formatted.c_str(), snap.size());
    return 0;
}
