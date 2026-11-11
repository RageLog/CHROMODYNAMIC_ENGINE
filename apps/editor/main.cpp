// =============================================================================
// CHROMODYNAMIC STUDIO -- apps/editor/main.cpp
//
// Thin Source 2 toolframework2-modeled entry point (< 30 LOC, ADR-20261112).
// Delegates CLI parsing + headless smoke/export/schema actions to
// cd::studio::parse_and_run_early_cli and lifecycle execution to StudioApp.
// =============================================================================
#include "StudioApp.hpp"
#include "core/StudioConfig.hpp"

#include <cd/app/run.hpp>

#include <utility>

int main(int argc, char** argv)
{
    auto cli = cd::studio::parse_and_run_early_cli(argc, argv);
    if (cli.early_exit_code.has_value())
    {
        return *cli.early_exit_code;
    }
    return cd::app::run<StudioApp>(argc, argv, std::move(cli.config));
}
