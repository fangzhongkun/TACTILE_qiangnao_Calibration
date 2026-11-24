#ifndef TOUCH_FILTERING_H
#define TOUCH_FILTERING_H

#include "matrix.h"
#include "types.h"

#include <concepts>

namespace touch
{
    enum class border_t
    {
        constant,
        reflect,
        replicate,
        circular,
    };
    template<typename T> void remove_isolated_point(matrix<T>& frame, T threshold);

    template<typename T> void remove_isolated_point(matrix<T>& frame, const matrix<T>& threshold);

    //
    template<typename T>
    matrix<T> box_blur(const matrix<T>& m, size<int> kernel_size, border_t border_type = border_t::reflect);

    template<typename T>
    matrix<T> gaussian_blur(const matrix<T>& m, size<int> kernel_size, float sigma,
                            border_t border_type = border_t::reflect);

    template<typename T> matrix<T> median_blur(const matrix<T>& m, size<int> kernel_size);

    template<typename T>
    matrix<T> bilateral_blur(const matrix<T>& m, size<int> kernel_size, double sigma_color,
                             double sigma_space, border_t border_type = border_t::reflect);

    // morphology
    enum class morph_t
    {
        ERODE    = 0,
        DILATE   = 1,
        OPEN     = 2,
        CLOSE    = 3,
        GRADIENT = 4,
        TOPHAT   = 5,
        BLACKHAT = 6,
        HITMISS  = 7
    };

    template<typename T>
    matrix<T> morphology(const matrix<T>& mat, const matrix<T>& kernel, morph_t method);

} // namespace touch

#include "filtering.hpp"

#endif //! TOUCH_FILTERING_H