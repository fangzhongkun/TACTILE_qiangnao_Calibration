#ifndef TOUCH_PROCESSING_H
#define TOUCH_PROCESSING_H

#include "matrix.h"
#include "types.h"

#include <vector>

namespace touch
{
    enum class threshold_t : uint8_t
    {
        TRUNC  = 0x01,
        TOZERO = 0x02,
    };

    std::string to_string(threshold_t);

    template<typename T> void threshold(matrix<T>& input, T threshold, threshold_t type);
    template<typename T> void threshold(matrix<T>& input, const matrix<T>& threshold, threshold_t type);

    //
    template<typename T>
    std::vector<touch<T>> connected_components(const matrix<T>& frame, matrix<uint32_t>& labels,
                                               int connectivity = 8);

    //
    template<typename T>
    std::vector<int> mapping(const std::vector<point<T>>& t1, const std::vector<point<T>>& t2);

    //
    template<typename T, typename T2> matrix<T> crop(const matrix<T>& input, const rect<T2>& r);
} // namespace touch

#include "processing.hpp"

#endif //! TOUCH_PROCESSING_H
