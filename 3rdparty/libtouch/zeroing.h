#ifndef LIBTOUCH_ZEROING_H
#define LIBTOUCH_ZEROING_H

#include "matrix.h"
#include "processing.h"

#include <cstdint>

namespace touch
{
    struct zeroing
    {
        enum class adaptive_t : uint8_t
        {
            MAX  = 0x01,
            MEAN = 0x02,
        };

        // parameters
        adaptive_t  method{ adaptive_t::MAX };
        uint16_t    frames{ 16 };
        float       alpha{ 1.0 };
        uint16_t    static_beta{ 0 };
        float       dynamic_beta{ 0.0 };
        threshold_t type{ threshold_t::TOZERO };

        //
        uint16_t         index{ 0 };
        matrix<uint16_t> offset{};

        int32_t process(matrix<uint16_t>& frame);

        void reset();
    };

    std::string to_string(zeroing::adaptive_t);

} // namespace touch

#endif //! LIBTOUCH_ZEROING_H
