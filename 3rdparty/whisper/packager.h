#ifndef WHISPER_PACKAGER_H
#define WHISPER_PACKAGER_H

#include "payload.h"

#include <functional>
#include <string>
#include <vector>

namespace whisper
{
    struct packager
    {
        virtual std::vector<uint8_t> pack(uint8_t channel, uint8_t flags, const payload& payload) = 0;
        virtual ~packager()                                                                       = default;

        virtual void unpack(const void *buffer, size_t size) = 0;

        std::function<void(uint8_t channel, uint8_t flags, payload& payload)> onready{};

        std::function<void(const std::string& message)> onerror = [](auto&&) {};
    };
} // namespace whisper

#endif //! WHISPER_PACKAGER_H
