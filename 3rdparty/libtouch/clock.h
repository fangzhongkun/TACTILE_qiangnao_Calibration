#ifndef LIBTOUCH_CLOCK_H
#define LIBTOUCH_CLOCK_H

#ifdef ZYNQ
#include "xtime_l.h"
#endif

#include <chrono>
#include <cstdint>

using namespace std::chrono_literals;

namespace touch::clock
{
    inline std::chrono::nanoseconds ns()
    {
        using namespace std::chrono;

#ifdef ZYNQ
        XTime time{};
        XTime_GetTime(&time);
        return microseconds((time * 1000) / (COUNTS_PER_SECOND / 1000000));
#else
        return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch());
#endif
    }

    inline std::chrono::microseconds us()
    {
        using namespace std::chrono;

#ifdef ZYNQ
        XTime time{};
        XTime_GetTime(&time);
        return microseconds(time / (COUNTS_PER_SECOND / 1000000));
#else
        return duration_cast<microseconds>(steady_clock::now().time_since_epoch());
#endif
    }

    inline std::chrono::milliseconds ms()
    {
        using namespace std::chrono;

#ifdef ZYNQ
        XTime time{};
        XTime_GetTime(&time);
        return milliseconds(time / (COUNTS_PER_SECOND / 1000));
#else
        return duration_cast<milliseconds>(steady_clock::now().time_since_epoch());
#endif
    }
} // namespace touch::clock

#endif //! LIBTOUCH_CLOCK_H
