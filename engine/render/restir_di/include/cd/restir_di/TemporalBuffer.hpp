// =============================================================================
// CHROMODYNAMIC — cd/restir_di/TemporalBuffer.hpp
// ReSTIR DI — double-buffered per-pixel reservoir ring.
//
// Hosts two flat arrays of reservoirs (current frame + previous frame).
// Each frame: swap() ping-pongs the buffers so the GPU can read from
// "previous" while writing to "current" without aliasing.
//
// Reservoirs older than `max_age` frames are invalidated (zero-state)
// before the caller can access them — mimicking GPU-side age tracking
// in the temporal reuse compute shader (kRestirDiTemporalReuseCS).
//
// This header is CPU-only and is intentionally header-only so it can
// be used in unit tests without a GPU context.
//
// Reference: Bitterli et al. 2020, §4.3 — "History clamping".
// =============================================================================
#pragma once

#include <cd/restir_di/Reservoir.hpp>

#include <cstdint>
#include <vector>

namespace cd::restir_di
{

/// Double-buffered per-pixel reservoir store for temporal reuse.
/// R must be a type that has:
///   - a default constructor yielding zero / invalid state.
///   - void invalidate() noexcept  (sets it to zero-state).
///   - std::uint32_t age field.
template <typename R = Reservoir>
class TemporalBuffer
{
public:
    /// Construct with screen dimensions and maximum tolerated reservoir age.
    explicit TemporalBuffer(std::uint32_t width,
                            std::uint32_t height,
                            std::uint32_t max_age = 30U)
        : m_width(width)
        , m_height(height)
        , m_max_age(max_age)
        , m_front(static_cast<std::size_t>(width) * height)
        , m_back (static_cast<std::size_t>(width) * height)
    {}

    /// Width in pixels.
    [[nodiscard]] std::uint32_t width()  const noexcept { return m_width;  }
    /// Height in pixels.
    [[nodiscard]] std::uint32_t height() const noexcept { return m_height; }
    /// Total pixel count.
    [[nodiscard]] std::size_t   size()   const noexcept { return m_front.size(); }

    // ---- Current frame (write destination) ----------------------------------

    /// Mutable access to the current-frame reservoir for pixel (x, y).
    [[nodiscard]] R& current(std::uint32_t x, std::uint32_t y) noexcept
    {
        return m_front[index(x, y)];
    }

    /// Const access to the current-frame reservoir for pixel (x, y).
    [[nodiscard]] const R& current(std::uint32_t x, std::uint32_t y) const noexcept
    {
        return m_front[index(x, y)];
    }

    // ---- Previous frame (read source) ---------------------------------------

    /// Read the previous-frame reservoir for pixel (x, y).
    /// Returns the zero-state sentinel if the stored age exceeds max_age,
    /// so callers never need to check validity independently.
    [[nodiscard]] R previous(std::uint32_t x, std::uint32_t y) const noexcept
    {
        const R& r = m_back[index(x, y)];
        if (r.age > m_max_age) return R{};
        return r;
    }

    // ---- Frame boundary -----------------------------------------------------

    /// Promote current reservoirs to previous.
    /// Must be called exactly once per frame after the GPU temporal-reuse
    /// pass has written its merged output (with updated age fields) into
    /// current().  The age field is managed by the GPU shader (or test
    /// code) — TemporalBuffer does not modify age during swap.
    void swap() noexcept
    {
        m_front.swap(m_back);
    }

    /// Reset all reservoirs in both buffers to zero / invalid state.
    void clear() noexcept
    {
        for (R& r : m_front) r.invalidate();
        for (R& r : m_back)  r.invalidate();
    }

private:
    [[nodiscard]] std::size_t index(std::uint32_t x, std::uint32_t y) const noexcept
    {
        return static_cast<std::size_t>(y) * m_width + x;
    }

    std::uint32_t  m_width;
    std::uint32_t  m_height;
    std::uint32_t  m_max_age;
    std::vector<R> m_front;  ///< current frame (write)
    std::vector<R> m_back;   ///< previous frame (read)
};

}  // namespace cd::restir_di
