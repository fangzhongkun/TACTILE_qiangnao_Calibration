#include "zeroing.h"

namespace touch
{
    int32_t zeroing::process(matrix<uint16_t>& frame)
    {
        // alpha * A + beta
        if (frames > 0) {
            // feed frames
            if (index < frames) {
                if (offset.empty()) offset = frame.clone();

                if (index < frames) {
                    switch (method) {
                    case adaptive_t::MEAN:
                        offset = offset * (1 - 1.0 / frames) + frame * (1.0 / frames);
                        break;
                    case adaptive_t::MAX: offset = max(offset, frame);
                    default:              break;
                    }

                    index++;
                }

                if (index == frames) {
                    offset = offset * alpha + static_beta;
                }
            }

            // zero
            if (index >= frames) {
                const auto dyn_beta = static_cast<uint16_t>(frame.max() * std::clamp<float>(dynamic_beta, 0.0, 1.5));
                threshold(frame, offset + dyn_beta, type);
            }

            return index - frames;
        }

        // beta
        threshold(frame, static_beta, type);
        return 0;
    }

    void zeroing::reset()
    {
        offset = {};
        index  = 0;
    }

    std::string to_string(zeroing::adaptive_t method)
    {
        switch (method) {
        case zeroing::adaptive_t::MAX:  return "MAX";
        case zeroing::adaptive_t::MEAN: return "MEAN";
        default:                        return "UNKNOWN";
        }
    }
} // namespace touch
