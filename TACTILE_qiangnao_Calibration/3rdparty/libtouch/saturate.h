#ifndef TOUCH_SATURATE_H
#define TOUCH_SATURATE_H

#include <algorithm>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <limits>

namespace touch
{
    template<typename T1, typename T2>
    requires std::same_as<T1, T2>
    inline T1 saturate_cast(T2 v)
    {
        return v;
    }

    template<typename T1, typename T2>
    requires(!std::same_as<T1, T2>) && std::integral<T2>
    inline T1 saturate_cast(T2 v)
    {
        // [
        if constexpr (std::numeric_limits<T1>::min() > std::numeric_limits<T2>::min()) {
            v = std::max<T2>(std::numeric_limits<T1>::min(), v);
        }

        // ]
        if constexpr (std::numeric_limits<T1>::max() < std::numeric_limits<T2>::max()) {
            v = std::min<T2>(std::numeric_limits<T1>::max(), v);
        }

        return static_cast<T1>(v);
    }

    template<typename T1, typename T2>
    requires(!std::same_as<T1, T2>) && std::is_floating_point_v<T2>
    inline T1 saturate_cast(T2 v)
    {
        return saturate_cast<T1>(std::llround(v));
    }
} // namespace touch

#endif //! TOUCH_SATURATE_H
