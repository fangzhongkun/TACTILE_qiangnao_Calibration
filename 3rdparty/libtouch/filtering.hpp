#ifndef TOUCH_FILTERING_HPP
#define TOUCH_FILTERING_HPP

namespace touch
{
    template<typename T> void remove_isolated_point(matrix<T>& frame, T threshold)
    {
        for (int r = 0; r < frame.rows; ++r) {
            auto rptr = frame.ptr(r);
            for (int c = 0; c < frame.cols; ++c) {

                if (rptr[c] > threshold) {
                    // T
                    if (r > 0 && frame(r - 1, c) > threshold) continue;
                    // L
                    if (c > 0 && rptr[c - 1] > threshold) continue;
                    // R
                    if (c < frame.cols - 1 && rptr[c + 1] > threshold) continue;
                    // B
                    if (r < frame.rows - 1 && frame(r + 1, c) > threshold) continue;
                }

                rptr[c] = 0;
            }
        }
    }

    template<typename T> void remove_isolated_point(matrix<T>& frame, const matrix<T>& threshold)
    {
        assert(frame.rows == threshold.rows && frame.cols == threshold.cols);

        for (int r = 0; r < frame.rows; ++r) {
            auto rptr = frame.ptr(r);
            auto tptr = threshold.ptr(r);
            for (int c = 0; c < frame.cols; ++c) {

                if (rptr[c] > tptr[c]) {
                    // T
                    if (r > 0 && frame(r - 1, c) > threshold(r - 1, c)) continue;
                    // L
                    if (c > 0 && rptr[c - 1] > tptr[c - 1]) continue;
                    // R
                    if (c < frame.cols - 1 && rptr[c + 1] > tptr[c + 1]) continue;
                    // B
                    if (r < frame.rows - 1 && frame(r + 1, c) > threshold(r + 1, c)) continue;
                }

                rptr[c] = 0;
            }
        }
    }
} // namespace touch

#endif //! TOUCH_FILTERING_HPP