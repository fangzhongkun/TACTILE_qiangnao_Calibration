#ifndef SCANNER_SENSOR_H
#define SCANNER_SENSOR_H

#include <cstdint>

struct Sensor
{
    uint16_t width{ 0 };
    uint16_t height{ 0 };

    uint8_t format{ 0 };

    uint16_t physical_width{ 0 };
    uint16_t physical_height{ 0 };

    touch::ratio<uint8_t> sar{ 1, 1 };
};

#endif //! SCANNER_SENSOR_H