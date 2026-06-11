// =============================================================================
// CHROMODYNAMIC — cd/core/Ref.hpp
// Phase 82.B / Wave 250 — non-owning, never-null reference wrapper.
//
// `Ref<T>` is `std::reference_wrapper<T>` with a more explicit name +
// implicit conversion to `T&`. It documents "this is a non-owning,
// never-null reference" — clearer than `T*` (which can be null and
// could outlive the referent in test setup) or `T&` (which can't be
// stored in containers).
//
//   class System {
//       Ref<World> world_;
//       Ref<EventBus> bus_;
//   };
//
// Implicit construction from `T&`; cannot construct from `T*` (no
// nullable). `get()` returns `T&` for explicit unwrapping.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <type_traits>

namespace cd::core
{

template <class T>
class Ref
{
public:
    // NOLINTNEXTLINE(google-explicit-constructor) — implicit construction
    // from T& mirrors std::reference_wrapper; that conversion is the point.
    constexpr Ref(T& r) noexcept : ptr_ { &r } {}

    Ref(T&&) = delete;   // No binding to temporaries.

    [[nodiscard]] constexpr T& get() const noexcept { return *ptr_; }

    // NOLINTNEXTLINE(google-explicit-constructor) — see ctor note above.
    constexpr operator T&() const noexcept { return *ptr_; }

    [[nodiscard]] constexpr T* operator->() const noexcept { return ptr_; }

private:
    T* ptr_;
};

}  // namespace cd::core
