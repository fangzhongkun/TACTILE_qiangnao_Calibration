#include "combination.h"

#include <cassert>

namespace touch
{
    // n!
    // TODO: std::tgamma(N + 1)
    uint64_t factorial(const int N)
    {
        assert(N >= 0 && N <= 20);

        if (N <= 0) return 1;

        uint64_t F = N;

        for (int i = N - 1; i > 0; --i) F *= i;

        return F;
    }

    // $ A^m_n = \frac{n!}{(n - m)!} $
    uint64_t permutation(const int N, const int M)
    {
        assert(N >= 0 && N >= M);

        if (N < M || M <= 0) return 1;

        uint64_t P = N;

        for (int i = N - 1; i > N - M; --i) P *= i;

        return P;
    }

    // $ C^m_n = \frac{n!}{(n - m)!m!} $
    uint64_t combination(const int N, const int M) { return permutation(N, M) / factorial(M); }

    matrix<int> combinations(const int N, const int M)
    {
        const uint64_t L = touch::combination(N, M); // max length

        assert(L <= std::numeric_limits<int>::max());

        matrix<int> indices(static_cast<int>(L), M);

        for (size_t count = 0; count < indices.size();) {
            const int C = static_cast<int>(count / L);                                // col
            const int I = N - M + C;                                                  // max index
            int       i = (C < 1) ? -1 : indices(static_cast<int>(count % L), C - 1); // index
            for (; i < I; ++i) {
                const auto ni = touch::combination(N - 2 - i, M - C - 1);
                for (uint64_t r = count; r < count + ni; ++r) {
                    indices(static_cast<int>(r % L), C) = i + 1;
                }
                count += ni;
            }
        }

        return indices;
    }
} // namespace touch