#ifndef LIBTOUCH_COMBINATION_H
#define LIBTOUCH_COMBINATION_H

#include "matrix.h"

#include <cstdint>

namespace touch
{
    uint64_t factorial(int N);

    uint64_t permutation(int N, int M);

    uint64_t combination(int N, int M);

    // combination indices
    matrix<int> combinations(int N, int M);
} // namespace touch

#endif //! LIBTOUCH_COMBINATION_H