# cd::diag

## Purpose
Diagnostics layer providing assertion macros (CD_ASSERT, CD_VERIFY, CD_PANIC), structured panic handling, crash reporting, and deadline monitoring for development and runtime error detection.

## Namespace
`cd::diag::` — all public symbols.

## Public headers
- `Assert.hpp` — Assertion macros + panic handler setup
- `CrashReporter.hpp` — Native crash report generation
- `DeadlineMonitor.hpp` — Watchdog timer for missed frame deadlines

## Primary types
- `PanicInfo` — Structured error details (file, line, expression, optional message)
- Panic handler callback; optional backtrace capture
- Deadline monitor state machine

## Usage example
```cpp
#include <cd/diag/Assert.hpp>

// Debug-only check
CD_ASSERT(ptr != nullptr);

// Release-active check
CD_VERIFY(result.is_ok());

// Unconditional panic
CD_PANIC("This should never be reached");

// Custom handler (e.g., for tests)
cd::diag::set_panic_handler([](const cd::diag::PanicInfo& info) {
  throw std::runtime_error(info.message);
});
```

## Build
```bash
cmake --build --preset ninja-debug --target cd_diag
```

## Test
```bash
ctest --preset ninja-debug -R diag
```

## Dependencies (per CMakeLists)
- `cd::core` — ErrorCode, Result types
- `cd::platform` — Stack trace capture (optional)

## Notes
- Static library
- CD_ASSERT is debug-only (stripped in release)
- CD_VERIFY is always active (used in release builds)
- Default panic handler writes to stderr and aborts
- Sprint S2.1.e — ADR-013 (diagnostics) + ADR-017 (foundation pattern)

## References
- ADR-013 — Diagnostics and telemetry architecture
- ADR-017 — Foundation policy bundle
