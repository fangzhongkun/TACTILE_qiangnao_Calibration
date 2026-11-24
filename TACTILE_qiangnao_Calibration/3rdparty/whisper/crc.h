#ifndef WHISPER_CRC_H
#define WHISPER_CRC_H

#include <cstddef>
#include <cstdint>

namespace whisper
{
    uint16_t crc_16(const uint8_t *data, size_t length);

    uint16_t crc_modbus(const uint8_t *data, size_t length);
} // namespace whisper

#endif //! WHISPER_CRC_H
