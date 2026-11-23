#ifdef __linux__

#include "serial.h"

#include <fcntl.h>
#include <probe/thread.h>
#include <termios.h>
#include <unistd.h>

namespace serial
{
    serial_port::~serial_port()
    {
        abort();

        if (rx_thread_.joinable()) rx_thread_.join();

        // close serial port
        ::close(fd_);
    }

    int serial_port::open(const std::string& port, uint32_t baudrate, uint8_t databits, uint8_t stopbits,
                          uint8_t parity)
    {
        fd_ = ::open(port.c_str(), O_RDONLY | O_NOCTTY | O_SYNC);
        if (fd_ == -1) {
            return -1;
        }

        struct termios options;
        tcgetattr(fd_, &options);

        options.c_cflag |= (CLOCAL | CREAD);

        // baudrate
        cfsetspeed(&options, baudrate);

        // databits: 8
        options.c_cflag &= ~CSIZE;
        options.c_cflag |= CS8;

        // parity: none
        options.c_cflag &= ~PARENB;

        // stopbits: 1
        options.c_cflag &= ~CSTOPB;

        // raw mode: disable all special processing of input and output characters
        cfmakeraw(&options);

        // clear
        tcflush(fd_, TCIOFLUSH);

        tcsetattr(fd_, TCSANOW, &options);

        running_ = true;

        rx_thread_ = std::thread([=, this]() {
            probe::thread::set_name("SERIAL-RX");

            constexpr int BUFFER_SIZE = 64;
            uint8_t       buffer[BUFFER_SIZE]{};

            while (running_) {
                auto n = ::read(fd_, buffer, BUFFER_SIZE);
                if (n <= 0) continue;

                onrx(buffer, n);
            }
        });

        return 0;
    }

    void serial_port::abort() { running_ = false; }

    uint32_t serial_port::tx(const void *buffer, uint16_t size) { return 0; }
} // namespace serial

#endif