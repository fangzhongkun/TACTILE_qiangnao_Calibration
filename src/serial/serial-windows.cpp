#ifdef _WIN32

#include "logging.h"
#include "serial.h"

#include <fmt/format.h>
#include <probe/thread.h>
#include <probe/util.h>

namespace serial
{
    serial_port::~serial_port()
    {
        if (STOP_EVENT_.get() != INVALID_HANDLE_VALUE) ::SetEvent(STOP_EVENT_.get());

        abort();

        // close the receiving thread & IO
        if (rx_thread_.joinable()) {
            if (handle_.get() != INVALID_HANDLE_VALUE) {
                ::CancelIoEx(handle_.get(), &rovl_);
            }

            rx_thread_.join();
        }
    }

    int serial_port::open(const options& options)
    {
        return open(options.port, options.baudrate, options.databits, options.stopbits, options.parity);
    }

    int serial_port::open(const std::string& port, const uint32_t baudrate, const uint8_t databits,
                          const uint8_t stopbits, const uint8_t parity)
    {
        handle_.attach(::CreateFile(probe::util::to_utf16(port).c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                                    nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr));
        if (handle_.get() == INVALID_HANDLE_VALUE) {
            loge("[SERIAL@{{{}}}] CreateFile: {}", port,
                 probe::util::format_system_error(::GetLastError()));
            return -1;
        }

        DCB params{ .DCBlength = sizeof(DCB) };
        if (!::GetCommState(handle_.get(), &params)) {
            handle_.close();
            loge("[SERIAL@{{{}}}] GetCommState: {}", port,
                 probe::util::format_system_error(::GetLastError()));
            return -1;
        }

        // common
        params.fBinary       = TRUE;  // Win32 only supports binary mode.
        params.fNull         = FALSE; // Do not ignore NULL characters.
        params.fAbortOnError = FALSE; // Ignore serial framing errors.
        params.fErrorChar    = FALSE;

        // baudrate
        params.BaudRate = baudrate;

        // databits
        params.ByteSize = databits;

        // parity
        params.Parity  = parity;
        params.fParity = (parity != NOPARITY) ? TRUE : FALSE;

        // stopbits
        params.StopBits = stopbits;

        // flow control: none
        params.fOutxCtsFlow      = FALSE;
        params.fOutxDsrFlow      = FALSE;
        params.fTXContinueOnXoff = TRUE;
        params.fDsrSensitivity   = FALSE;
        params.fOutX             = FALSE;
        params.fInX              = FALSE;
        params.fDtrControl       = DTR_CONTROL_ENABLE;
        params.fRtsControl       = RTS_CONTROL_ENABLE;

        if (!::SetCommState(handle_.get(), &params)) {
            handle_.close();
            loge("[SERIAL@{{{}}}] SetCommState: {}", port,
                 probe::util::format_system_error(::GetLastError()));
            return -1;
        }

        COMMTIMEOUTS timeouts{ .ReadIntervalTimeout = 1 };

        if (!::SetCommTimeouts(handle_.get(), &timeouts)) {
            handle_.close();
            loge("[SERIAL@{{{}}}] SetCommTimeouts: {}", port,
                 probe::util::format_system_error(::GetLastError()));
            return -1;
        }

        // clear
        ::PurgeComm(handle_.get(), PURGE_TXCLEAR | PURGE_RXCLEAR);
        ::ClearCommError(handle_.get(), nullptr, nullptr);

        running_   = true;
        rx_thread_ = std::thread([=, this]() {
            probe::thread::set_name("SERIAL-RX");

            const winrt::handle ARRIVED_EVENT{ ::CreateEvent(nullptr, TRUE, FALSE, nullptr) };
            STOP_EVENT_.attach(::CreateEvent(nullptr, TRUE, FALSE, nullptr));
            if (ARRIVED_EVENT.get() == INVALID_HANDLE_VALUE || STOP_EVENT_.get() == INVALID_HANDLE_VALUE) {
                abort();
                loge("[SERIAL@{{{}}}] failed to create events: {}", port,
                     probe::util::format_system_error(::GetLastError()));
                return;
            }

            const HANDLE events[] = { STOP_EVENT_.get(), ARRIVED_EVENT.get() };

            constexpr int BUFFER_SIZE = 512;
            uint8_t       buffer[BUFFER_SIZE]{};

            while (running_) {
                rovl_ = { .hEvent = ARRIVED_EVENT.get() };

                if (!::ReadFile(handle_.get(), buffer, BUFFER_SIZE, nullptr, &rovl_)) {
                    const auto ec = ::GetLastError();
                    if (ec != ERROR_IO_PENDING) {
                        abort();
                        loge("[SERIAL@{{{}}}] aborted: {}", port, probe::util::format_system_error(ec));
                        return;
                    }
                }

                DWORD bytes = 0;
                switch (::WaitForMultipleObjects(2, events, FALSE, INFINITE)) {
                case WAIT_OBJECT_0:
                    abort();
                    logi("[SERIAL@{{{}}}] stopped", port);
                    return;
                case WAIT_OBJECT_0 + 1:
                    if (!::GetOverlappedResult(handle_.get(), &rovl_, &bytes, TRUE)) {
                        const auto ec = ::GetLastError();
                        if (ec == ERROR_OPERATION_ABORTED || ec == ERROR_ACCESS_DENIED ||
                            ec == ERROR_BAD_COMMAND || ec == ERROR_DEVICE_REMOVED ||
                            ec == ERROR_INVALID_HANDLE) {
                            abort();
                            loge("[SERIAL@{{{}}}] aborted: {}", port, probe::util::format_system_error(ec));
                            return;
                        }
                        else {
                            loge("[SERIAL@{{{}}}] failed to read data: {}", port,
                                 probe::util::format_system_error(ec));
                            continue;
                        }
                    }
                    break;
                default:
                    abort();
                    loge("[SERIAL@{{{}}}] aborted: {}", port,
                         probe::util::format_system_error(::GetLastError()));
                    return;
                }

                if (bytes > 0) {
                    for (auto& hook : before_rx_hooks) hook(reinterpret_cast<const char *>(buffer), bytes);
                    rx(buffer, bytes);
                }
            }
        });

        LOG(INFO) << fmt::format("[SERIAL@{{{}}}] opened successfully", port);

        ready_ = true;

        return 0;
    }

    void serial_port::abort()
    {
        running_ = false;

        if (ready_) {
            ready_ = false;
            onaborted();
        }
    }

    // synchronous
    uint32_t serial_port::tx(const void *buffer, const size_t size)
    {
        const winrt::handle EMPTY_EVENT{ ::CreateEvent(nullptr, TRUE, FALSE, nullptr) };
        OVERLAPPED          ovl = { .hEvent = EMPTY_EVENT.get() };

        if (EMPTY_EVENT.get() == INVALID_HANDLE_VALUE) {
            loge("[SERIAL] failed to create event: {}", probe::util::format_system_error(::GetLastError()));
            return 0;
        }

        for (auto& hook : before_tx_hooks) hook(reinterpret_cast<const char *>(buffer), size);

        if (!::WriteFile(handle_.get(), buffer, static_cast<DWORD>(size), nullptr, &ovl)) {
            const auto ec = ::GetLastError();
            if (ec != ERROR_IO_PENDING) {
                loge("[SERIAL] WriteFile: {}", probe::util::format_system_error(ec));
                return 0;
            }
        }

        DWORD bytes = 0;
        ::WaitForSingleObject(ovl.hEvent, INFINITE);
        if (!::GetOverlappedResult(handle_.get(), &ovl, &bytes, TRUE)) {
            loge("[SERIAL] GetOverlappedResult: {}", probe::util::format_system_error(::GetLastError()));
            return 0;
        }

        return bytes;
    }
} // namespace serial

#endif