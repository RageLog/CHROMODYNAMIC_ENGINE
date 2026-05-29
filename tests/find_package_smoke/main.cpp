// =============================================================================
// tests/find_package_smoke/main.cpp
// Phase 10 / Sprint 4 / Wave 121 -- downstream find_package() consumer.
//
// Exercises one symbol from each foundation library that the install
// tree is expected to ship: cd::core (ErrorCode + format), cd::diag
// (CD_VERIFY against a known-true expression), cd::log (RingBufferSink).
//
// Marathon Run 24 X8 extension: also exercises cd::editor_panel (a
// header-only INTERFACE library shipped in Run 23 N5A) so the install
// path covers the INTERFACE target case end-to-end.
//
// Exit 0 means the prefix-installed CHROMODYNAMIC is usable from a
// standalone consumer project without rebuilding the engine.
// =============================================================================

#include <cd/core/ErrorCode.hpp>
#include <cd/core/ErrorFormat.hpp>
#include <cd/diag/Assert.hpp>
#include <cd/editor_panel/CompositeFxBinding.hpp>
#include <cd/editor_panel/CompositePresets.hpp>
#include <cd/log/RingBufferSink.hpp>

#include <cstdint>
#include <cstdio>
#include <string>

int main()
{
    // cd::core -- construct an ErrorCode and format it.
    const auto ec = cd::core::core_errors::make(
        cd::core::core_errors::Code::kNotFound, "smoke");
    const auto formatted = cd::core::format(ec);
    if (formatted != "core::NotFound: smoke") {
        std::fprintf(stderr, "format() returned %s\n", formatted.c_str());
        return 1;
    }

    // cd::diag -- verify a true expression.
    CD_VERIFY(1 + 1 == 2);

    // cd::log -- push a record into a ring buffer and read it back.
    cd::log::RingBufferSink sink { 4 };
    cd::log::LogRecord r;
    r.message = "find_package smoke";
    sink.on_log_record(r);
    const auto snap = sink.snapshot();
    if (snap.size() != 1 || snap[0].message != "find_package smoke") {
        std::fprintf(stderr, "RingBufferSink snapshot mismatch\n");
        return 1;
    }

    // cd::editor_panel -- INTERFACE library, header-only.  Bind a
    // local float and confirm the Cinematic preset writes the
    // expected exposure value (2.5F).
    float exposure_under_binding { 0.0F };
    std::int32_t tonemap_under_binding { -1 };
    const cd::editor_panel::CompositeFxBinding binding {
        .exposure   = &exposure_under_binding,
        .tonemap_op = &tonemap_under_binding,
    };
    cd::editor_panel::apply(binding, cd::editor_panel::PresetId::kCinematic);
    if (exposure_under_binding != 2.5F) {
        std::fprintf(stderr, "editor_panel Cinematic preset failed exposure check: got %f\n",
                     static_cast<double>(exposure_under_binding));
        return 1;
    }
    if (tonemap_under_binding != static_cast<std::int32_t>(cd::editor_panel::TonemapOp::kHable)) {
        std::fprintf(stderr, "editor_panel Cinematic preset failed tonemap check: got %d\n",
                     tonemap_under_binding);
        return 1;
    }

    std::fprintf(stdout,
                 "find_package(CHROMODYNAMIC) smoke OK -- formatted=%s, ring size=%zu, "
                 "editor_panel exposure=%.2f tonemap=%d\n",
                 formatted.c_str(), snap.size(),
                 static_cast<double>(exposure_under_binding),
                 tonemap_under_binding);
    return 0;
}
