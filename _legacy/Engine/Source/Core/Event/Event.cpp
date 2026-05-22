#include "./Core/Event/Event.hpp"

namespace CD {
Event::~Event() noexcept = default;
constexpr bool Event::isHasCategory(const EventCategory& category) const
{
  return getCategoryFlags() & category;
}

}  // namespace CD
