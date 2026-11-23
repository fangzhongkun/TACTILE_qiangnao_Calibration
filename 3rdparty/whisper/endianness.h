#ifndef WHISPER_ENDIANNESS_H
#define WHISPER_ENDIANNESS_H

#include <concepts>
#include <cstdint>

namespace whisper
{
    enum class endianness_t : unsigned char
    {
        little,
        big
    };

    namespace endian
    {
        template<typename T>
        requires std::is_arithmetic_v<T> || std::is_enum_v<T>
        T byteswap(T x)
        {
            if constexpr (sizeof(T) == 2) {
                uint16_t _x = *reinterpret_cast<uint16_t *>(&x);

                uint16_t _z = ((_x >> 8) & 0x00ff) | ((_x & 0x00ff) << 8);

                return *reinterpret_cast<T *>(&_z);
            }
            else if constexpr (sizeof(T) == 4) {
                uint32_t _x = *reinterpret_cast<uint32_t *>(&x);

                uint32_t _z = ((_x & 0xff000000) >> 24) | ((_x & 0x00ff0000) >> 8) |
                              ((_x & 0x0000ff00) << 8) | ((_x & 0x000000ff) << 24);

                return *reinterpret_cast<T *>(&_z);
            }
            else if constexpr (sizeof(T) == 8) {
                uint64_t _x = *reinterpret_cast<uint64_t *>(&x);

                uint64_t _z = ((_x & 0xff00000000000000) >> 56) | ((_x & 0x00ff000000000000) >> 40) |
                              ((_x & 0x0000ff0000000000) >> 24) | ((_x & 0x000000ff00000000) >> 8) |
                              ((_x & 0x00000000ff000000) << 8) | ((_x & 0x0000000000ff0000) << 24) |
                              ((_x & 0x000000000000ff00) << 40) | ((_x & 0x00000000000000ff) << 56);

                return *reinterpret_cast<T *>(&_z);
            }
            else {
                return x;
            }
        }
    } // namespace endian

} // namespace whisper

#endif //! WHISPER_ENDIANNESS_H
