#ifndef WHISPER_PACKAGER_DEFAULT_H
#define WHISPER_PACKAGER_DEFAULT_H

#include "packager.h"

namespace whisper
{
    struct default_packager : public packager
    {
        std::vector<uint8_t> pack(uint8_t channel, uint8_t flags, const payload& data) override;

        void unpack(const void *buffer, size_t size) override;

    private:
        // state machine
        uint8_t state_{};

        //
        uint8_t  channel_{};
        uint8_t  flags_{};
        uint16_t length_{};
        payload  payload_{};
        uint16_t checksum_{};
    };
} // namespace whisper

#endif //! WHISPER_PACKAGER_DEFAULT_H
