#ifndef TOUCH_PROCESSING_HPP
#define TOUCH_PROCESSING_HPP

#include "combination.h"

#include <vector>

namespace touch
{
    template<typename T> void threshold(matrix<T>& input, T threshold, threshold_t type)
    {
        const int  LEN = input.rows * input.cols;
        const auto ptr = input.ptr();

        switch (type) {
        case threshold_t::TRUNC:
            for (int i = 0; i < LEN; ++i) {
                if (ptr[i] < threshold) {
                    ptr[i] = 0;
                }
            }
            break;

        case threshold_t::TOZERO:
        default:
            for (int i = 0; i < LEN; ++i) {
                ptr[i] = saturate_cast<T>(ptr[i] - threshold);
            }
            break;
        }
    }

    template<typename T> void threshold(matrix<T>& input, const matrix<T>& threshold, threshold_t type)
    {
        assert(input.rows == threshold.rows && input.cols == threshold.cols);

        const int  LEN  = input.rows * input.cols;
        const auto sptr = input.ptr();
        const auto tptr = threshold.ptr();

        switch (type) {
        case threshold_t::TRUNC:
            for (int i = 0; i < LEN; ++i) {
                if (sptr[i] < tptr[i]) {
                    sptr[i] = 0;
                }
            }
            break;

        case threshold_t::TOZERO:
        default:
            for (int i = 0; i < LEN; ++i) {
                sptr[i] = saturate_cast<T>(sptr[i] - tptr[i]);
            }
            break;
        }
    }

    //
    template<typename T>
    std::vector<touch<T>> connected_components(const matrix<T>& frame, matrix<uint32_t>& labels,
                                               int connectivity)
    {
        assert(connectivity == 4);
        (void)(connectivity);

        labels.create(frame.rows, frame.cols);
        labels.fill(0);

        std::vector<uint32_t> equivalent(1, 0); // equivalent list

        for (int r = 0; r < frame.rows; ++r) {

            auto      pfptr = frame.ptr(r) - frame.cols;
            auto      cfptr = frame.ptr(r);
            uint32_t *plptr = labels.ptr(r) - labels.cols;
            uint32_t *clptr = labels.ptr(r);

            for (int c = 0; c < frame.cols; ++c) {

                // - T -
                // L X -
                // - - -
                if (cfptr[c] > 0) { // X > 0
                    if (r > 0 && pfptr[c] > 0) {
                        // T > 0 && L > 0
                        if (c > 0 && cfptr[c - 1] > 0) {
                            clptr[c] = std::min(clptr[c - 1], plptr[c]);

                            equivalent[std::max(clptr[c - 1], plptr[c])] = equivalent[clptr[c]];
                        }
                        // T > 0 && L == 0
                        else {
                            clptr[c] = plptr[c];
                        }
                    }
                    else {
                        // T == 0 && L > 0
                        if (c > 0 && cfptr[c - 1] > 0) {
                            clptr[c] = clptr[c - 1];
                        }
                        // T == 0 && L == 0
                        else {
                            // new label
                            clptr[c] = static_cast<uint32_t>(equivalent.size());
                            equivalent.push_back(static_cast<uint32_t>(equivalent.size()));
                        }
                    }
                }
            }
        }

        std::vector<size_t> mapping(equivalent.size(), 0);
        size_t              nb_labels = 0;
        for (size_t i = 1; i < equivalent.size(); ++i) {
            if (equivalent[i] != equivalent[i - 1]) {
                mapping[equivalent[i]] = nb_labels;
                nb_labels++;
            }
        }

        std::vector<touch<T>> stats(nb_labels);

        for (int r = 0; r < labels.rows; ++r) {
            auto lptr = labels.ptr(r);
            auto fptr = frame.ptr(r);
            for (int c = 0; c < labels.cols; ++c) {
                const auto label = equivalent[lptr[c]];
                const auto index = mapping[label];

                if (label <= 0) continue;

                // statistics
                stats[index].area++;

                // left top
                if (stats[index].lt.x > c) stats[index].lt.x = c;
                if (stats[index].lt.y > r) stats[index].lt.y = r;
                // right bottom
                if (stats[index].rb.x < c) stats[index].rb.x = c;
                if (stats[index].rb.y < r) stats[index].rb.y = r;

                // peak value & position
                if (stats[index].value < fptr[c]) {
                    stats[index].peak  = { c, r };
                    stats[index].value = fptr[c];
                }
            }
        }

        return stats;
    }

    template<typename T>
    std::vector<int> mapping(const std::vector<point<T>>& t1, const std::vector<point<T>>& t2)
    {
        const auto& LT = t1.size() < t2.size() ? t2 : t1; // LONG  ONE
        const auto& ST = t1.size() < t2.size() ? t1 : t2; // SHORT ONE

        const int M = static_cast<int>(LT.size());
        const int N = static_cast<int>(ST.size());

        double dmin = std::numeric_limits<double>::max(); // minimum distance
        int    rmin = 0;

        auto C = combinations(M, N);
        for (int i = 0; i < C.rows; ++i) {

            double d = 0;
            for (int j = 0; j < N; j++) {
                auto dx = LT[C(i, j)].x - ST[j].x;
                auto dy = LT[C(i, j)].y - ST[j].y;
                d += dx * dx + dy * dy;
            }

            if (dmin > d) {
                dmin = d;
                rmin = i;
            }
        }

        std::vector<int> indices(N);
        for (int i = 0; i < N; ++i) {
            indices[i] = C(rmin, i);
        }

        return indices;
    }

    template<typename T, typename T2> matrix<T> crop(const matrix<T>& input, const rect<T2>& r)
    {
        assert(r.x < input.cols && r.y < input.rows);
        assert(r.width > 0 && r.height > 0);

        const int rows = std::min<int>(r.height, std::max<int>(0, input.rows - r.y));
        const int cols = std::min<int>(r.width, std::max<int>(0, input.cols - r.x));

        matrix<T> m(rows, cols);

        for (int i = 0; i < rows; ++i) {
            for (int j = 0; j < cols; ++j) {
                m(i, j) = input(i + r.y, j + r.x);
            }
        }

        return m;
    }

} // namespace touch

#endif //! TOUCH_PROCESSING_HPP
