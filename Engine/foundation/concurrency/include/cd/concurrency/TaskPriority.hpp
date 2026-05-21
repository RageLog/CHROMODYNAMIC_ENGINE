// =============================================================================
// CHROMODYNAMIC — cd/concurrency/TaskPriority.hpp
// ADR-015 + ADR-017 P1 (DfH common/threading/taskpriority.hpp salvage)
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>

namespace cd::concurrency
{

enum class TaskPriority : std::uint8_t
{
    Low = 0,
    Normal = 1,
    High = 2,
    Critical = 3,  // UI / safety-critical
};

}  // namespace cd::concurrency
