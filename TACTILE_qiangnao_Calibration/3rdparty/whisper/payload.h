#ifndef WHISPER_PAYLOAD_H
#define WHISPER_PAYLOAD_H

#include "endianness.h"

#include <concepts>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace whisper
{
    struct payload
    {
        payload() = default;

        explicit payload(const std::string& str)
        {
            data_.resize(str.size(), {});
            std::memcpy(data_.data(), str.data(), str.size());
        }

        payload(const void *buffer, const size_t size)
        {
            data_.resize(size, {});
            std::memcpy(data_.data(), buffer, size);
        }

        template<size_t N> explicit payload(const std::array<uint8_t, N>& buffer)
        {
            data_.resize(buffer.size(), {});
            std::memcpy(data_.data(), buffer.data(), buffer.size());
        }

        payload(const std::initializer_list<uint8_t> list)
        {
            data_.resize(list.size(), {});
            std::memcpy(data_.data(), std::data(list), list.size());
        }

        template<typename T, endianness_t E = endianness_t::little>
        requires std::is_arithmetic_v<T> || std::is_enum_v<T>
        payload& write(T value)
        {
            const size_t widx = data_.size();

            data_.resize(widx + sizeof(T), {});

            if constexpr (E == endianness_t::big) {
                value = endian::byteswap(value);
            }

            std::memcpy(data_.data() + widx, &value, sizeof(T));

            return *this;
        }

        payload& write(const void *buffer, const size_t size)
        {
            const size_t widx = data_.size();

            data_.resize(widx + size, {});

            std::memcpy(data_.data() + widx, buffer, size);

            return *this;
        }

        // read
        size_t ridx{};

        template<typename T, endianness_t E = endianness_t::little>
        requires std::is_arithmetic_v<T> || std::is_enum_v<T>
        T read()
        {
            if ((ridx + sizeof(T)) > data_.size()) return T{};

            T value = *reinterpret_cast<T *>(data_.data() + ridx);

            ridx += sizeof(T);

            if constexpr (E == endianness_t::big)
                return endian::byteswap(value);
            else
                return value;
        }

        // ignore endianness
        template<typename T>
        requires std::is_class_v<T>
        T read()
        {
            if ((ridx + sizeof(T)) > data_.size()) return T{};

            T value = *reinterpret_cast<T *>(data_.data() + ridx);

            ridx += sizeof(T);

            return value;
        }

        // return the address of the payload buffer
        [[nodiscard]] const uint8_t *read(size_t size)
        {
            const auto ptr = data_.data() + ridx;
            ridx += size;
            return ptr;
        }

        void seek(size_t offset) { ridx += offset; }

        //
        void clear()
        {
            data_.clear();
            ridx = 0;
        }

        //
        [[nodiscard]] auto size() const noexcept { return data_.size(); }

        [[nodiscard]] auto remain_size() const noexcept { return std::max<size_t>(0, data_.size() - ridx); }

        [[nodiscard]] const uint8_t *data() const noexcept { return data_.data(); }

        [[nodiscard]] const uint8_t *remain() const noexcept { return data_.data() + ridx; }

    private:
        std::vector<uint8_t> data_{};
    };
} // namespace whisper

#endif //! WHISPER_PAYLOAD_H
