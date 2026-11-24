#ifndef WHISPER_WHISPER_H
#define WHISPER_WHISPER_H

#include "packager-default.h"
#include "payload.h"

#include <functional>
#include <chrono>
#include <map>
#include <memory>
#include <set>
#if !defined(ZYNQ)
#include <condition_variable>
#include <mutex>
#endif

namespace whisper
{
    struct whisper
    {
        explicit whisper(const std::shared_ptr<packager>& pkger = std::make_shared<default_packager>());

        whisper(const whisper&)            = delete;
        whisper& operator=(const whisper&) = delete;

        // no response required
        void put(uint8_t channel, const payload& data);

        // require response
        void get(
            uint8_t channel, const payload& data, const std::function<void(payload&)>& handler,
            const std::function<void(const std::string&)>& onerror = [](auto) {});
        void get(
            uint8_t channel, const payload& data, const std::function<void(payload&)>& handler,
            const std::function<void(const std::string&)>& onerror,
            std::chrono::milliseconds timeout);

        // response for `get`
        void response(uint8_t channel, uint8_t flags, const payload& data);

        //
        void on(uint8_t channel, const std::function<payload(payload&)>& handler, bool put_only = false);

        // receive & send interface
        void rx(const void *buffer, size_t size);

        std::function<void(const void *, size_t)> tx = [](auto, auto) {};

        std::function<void(const std::string&)> onerror = [](auto) {};

    private:
        std::shared_ptr<packager>   pkger_{};
        std::map<uint8_t, uint32_t> cnt_{};

#if !defined(ZYNQ)
        std::mutex              mtx_;
        std::condition_variable cv_{};
#endif
        std::map<uint8_t, std::function<payload(payload&)>> handlers_{};
        std::set<uint8_t> put_only_channels_{};

        // channel, package_id, payload
        std::map<uint8_t, std::pair<uint8_t, payload>> response_{};
    };

} // namespace whisper

#endif //! WHISPER_WHISPER_H
