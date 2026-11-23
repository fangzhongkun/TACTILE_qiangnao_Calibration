#include "gesture.h"

namespace touch
{
    std::string to_string(gesture_t g)
    {
        using enum gesture_t;
        switch (g) {
        case PRESS:        return "PRESS";
        case LONGPRESS:    return "LONGPRESS";
        case RELEASE:      return "RELEASE";

        case GRASP:        return "GRASP";

        case MOVE:         return "MOVE";

        case DOUBLE_CLICK: return "DOUBLE CLICK";

        default:           return "N/A";
        }
    }
} // namespace touch