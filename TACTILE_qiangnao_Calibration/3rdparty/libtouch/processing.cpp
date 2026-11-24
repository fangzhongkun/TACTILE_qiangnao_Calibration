#include "processing.h"

namespace touch
{
    std::string to_string(threshold_t method)
    {
        switch (method) {
        case threshold_t::TOZERO: return "TOZERO";
        case threshold_t::TRUNC:  return "TRUNC";
        default:                  return "UNKNOWN";
        }
    }
} // namespace touch