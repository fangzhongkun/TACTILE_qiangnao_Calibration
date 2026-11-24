#ifndef SCANNER_DHS_DEFAULT_DEVICE_H
#define SCANNER_DHS_DEFAULT_DEVICE_H

#include "device.h"
#include "serial/options.h"

#include <atomic>
#include <thread>

struct DhsDefaultDevice final : public Device
{
    explicit DhsDefaultDevice(serial::options options) : Device(std::move(options)) { set_slave(0x11); }
    // int open() override;
    int     open(int dev) override;
    size_t  get_finger_size() override { return finger_size; }
    uint8_t get_id() override { return selected_protocol_id_.load(); }
    void    set_id(uint8_t id) override
    {
        const uint8_t part = static_cast<uint8_t>(id >> 4U);
        if ((part >= 1U) && (part <= 6U) && ((id & 0x0FU) == 0x01U)) {
            selected_protocol_id_.store(id);
            set_slave(id);
        }
    }
    void                      set_run(bool run) override;
    [[nodiscard]] std::string bus() const override { return "Whisper"; }

    [[nodiscard]] Capacities capacities() const override
    {
        return Capacities::ZEROING | Capacities::SAMPLE_STREAM | Capacities::LOGGING;
    }
    size_t finger_size = 0;

private:
    std::atomic<uint8_t> selected_protocol_id_{ 0x11 };
    std::atomic<bool>    running_{};
    std::jthread         thread_{};
};
#endif //! SCANNER_DHS_DEFAULT_DEVICE_H
