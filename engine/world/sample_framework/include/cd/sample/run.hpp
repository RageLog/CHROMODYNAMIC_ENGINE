// =============================================================================
// CHROMODYNAMIC -- cd/sample/run.hpp
// Mega-Marathon M2A skeleton (Run 29 / phase373).
//
// Template entry point. Sample main() typically reads:
//
//   #include <cd/sample/run.hpp>
//   #include "MyApp.hpp"   // : cd::sample::App
//   int main(int argc, char** argv) {
//       return cd::sample::run<MyApp>(argc, argv);
//   }
//
// The template constructs the App on the stack with the AppConfig the
// caller supplies (or AppConfig{} default), invokes App::run(), and
// returns the resulting exit code. Construction errors propagate as a
// non-zero exit.
// =============================================================================
#pragma once

#include <cd/sample/App.hpp>

#include <utility>

namespace cd::sample
{

/// Construct + drive an App-derived class. argc / argv are accepted for
/// future M2B+ extension (command-line config parsing) but currently
/// only forwarded into a sink so the signature is stable for samples.
template <class AppT, class... Args>
[[nodiscard]] int run(int /*argc*/, char** /*argv*/, Args&&... args)
{
    AppT app { std::forward<Args>(args)... };
    return app.run();
}

}  // namespace cd::sample
