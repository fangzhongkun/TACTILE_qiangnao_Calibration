#ifndef SCANNER_SERIAL_PORT_H
#define SCANNER_SERIAL_PORT_H

#include "options.h"

#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
// TODO: DO NOT INCLUDE <Windows.h> ON HEADER FILES
#include <Windows.h>
#include <winrt/base.h>
#endif

namespace serial
{
    class serial_port
    {
    public:
        serial_port() = default;

        serial_port(const serial_port&)            = delete;
        serial_port& operator=(const serial_port&) = delete;

        ~serial_port();

        //
        int open(const options& options);

        int open(const std::string& port, uint32_t baudrate, uint8_t databits, uint8_t stopbits,
                 uint8_t parity);

        bool ready() const { return ready_; }

        uint32_t tx(const void *buffer, size_t size);

        std::function<void(void *, size_t)> rx = [](auto, auto) {};

        std::function<void()>                        onaborted = [] {};
        std::function<void(int, const std::string&)> onerror   = [](auto&&...) {};

    private:
        void abort();

        // receiving thread
        std::atomic<bool> ready_{};
        std::atomic<bool> running_{};
        std::thread       rx_thread_{};

        // the file handle
#ifdef _WIN32
        winrt::handle handle_{};
        OVERLAPPED    rovl_{}; // for reading
        winrt::handle STOP_EVENT_{};
#elif __linux__
        int fd_{ -1 };
#endif
    };

    inline std::vector<std::function<void(const char *, size_t)>> before_tx_hooks{};
    inline std::vector<std::function<void(const char *, size_t)>> before_rx_hooks{};

    void hook_before_tx(const std::function<void(const char *, size_t)>&);
    void hook_before_rx(const std::function<void(const char *, size_t)>&);
} // namespace serial

#endif //! SCANNER_SERIAL_PORT_H