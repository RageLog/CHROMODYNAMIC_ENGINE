// =============================================================================
// CHROMODYNAMIC -- cd/app/run.hpp
// Application lifecycle entry template (cd::app_framework).
// =============================================================================
#pragma once

#include <cd/app/App.hpp>

#include <utility>

namespace cd::app
{

template <class AppT, class... Args>
[[nodiscard]] int run(int /*argc*/, char** /*argv*/, Args&&... args)
{
    AppT app { std::forward<Args>(args)... };
    return app.run();
}

}  // namespace cd::app
