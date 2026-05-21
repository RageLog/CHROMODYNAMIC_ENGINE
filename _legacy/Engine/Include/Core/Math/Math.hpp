#ifndef __MATH_H__
#define __MATH_H__

#include <concepts>
#include <type_traits>

#include "Define.hpp"

namespace CD
{
    template<typename T>
    concept Integral = std::is_integral_v<T>;
    
    template<Integral T>
    [[nodiscard]]constexpr auto CD_INLINE Mask(T x) -> decltype((1<<x)) 
    {
       return (1<<x);
    }
} // namespace CD


#endif // __MATH_H__