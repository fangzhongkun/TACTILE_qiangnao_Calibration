#ifndef SCANNER_SERIAL_OPTIONS_H
#define SCANNER_SERIAL_OPTIONS_H

#include <cstdint>
#include <string>

namespace serial
{
    struct options
    {
        std::string port{};
        uint32_t    baudrate{};
        uint8_t     databits{};
        uint8_t     stopbits{};
        uint8_t     parity{};
    };
} // namespace serial

#endif //! SCANNER_SERIAL_OPTIONS_H