#ifndef SCANNER_CLOCK_H
#define SCANNER_CLOCK_H

#include <chrono>

// clang-format off
#define OS_TIME_BASE        1000000000 // ns
#define OS_TIME_BASE_Q      { 1, OS_TIME_BASE }
// clang-format on

using namespace std::chrono_literals;

namespace av::clock
{
    constexpr auto nopts = std::chrono::nanoseconds::min(); // == AV_NOPTS_VALUE

    constexpr auto max = std::chrono::nanoseconds::max();
    constexpr auto min = std::chrono::nanoseconds::min();

    /**
     * Get the current time in nanoseconds since some unspecified starting point.
     * On platforms that support it, the time comes from a monotonic clock
     * This property makes this time source ideal for measuring relative time.
     * The returned values may not be monotonic on platforms where a monotonic
     * clock is not available.
     */
    inline auto ns()
    {
        using namespace std::chrono;
        return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch());
    }

    template<class Rep, class Period> auto ns(const std::chrono::duration<Rep, Period>& ts)
    {
        using namespace std::chrono;
        return duration_cast<nanoseconds>(ts);
    }

    inline auto us()
    {
        using namespace std::chrono;
        return duration_cast<microseconds>(steady_clock::now().time_since_epoch());
    }

    template<class Rep, class Period> auto us(const std::chrono::duration<Rep, Period>& ts)
    {
        using namespace std::chrono;
        return duration_cast<microseconds>(ts);
    }

    inline auto ms()
    {
        using namespace std::chrono;
        return duration_cast<milliseconds>(steady_clock::now().time_since_epoch());
    }

    template<class Rep, class Period> auto ms(const std::chrono::duration<Rep, Period>& ts)
    {
        using namespace std::chrono;
        return duration_cast<milliseconds>(ts);
    }

    inline auto s()
    {
        using namespace std::chrono;
        return duration_cast<seconds>(steady_clock::now().time_since_epoch());
    }

    template<class Rep, class Period> auto s(const std::chrono::duration<Rep, Period>& ts)
    {
        using namespace std::chrono;
        return duration_cast<seconds>(ts);
    }
} // namespace av::clock

#endif //! SCANNER_CLOCK_H