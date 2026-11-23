#ifndef SCANNER_DHS_MASTER_UINT16_DEVICE_H
#define SCANNER_DHS_MASTER_UINT16_DEVICE_H

#include "device.h"
#include "serial/options.h"

#include <atomic>
#include <thread>

struct DhsMasterUint16Device final : public Device
{
    explicit DhsMasterUint16Device(serial::options options) : Device(std::move(options)) {}

    ~DhsMasterUint16Device() override;

    // int open() override;
    int open(int dev ) override;

    size_t get_finger_size()override{
        return finger_size;
    }
    uint8_t get_id()override{
        return slave_;
    }
    void set_id(uint8_t id )override
    {
        slave_ =id;
    }
    void set_run(bool run)override
    {
        wait_ = run;
    }
    [[nodiscard]] std::string bus() const override { return "Whisper"; }

    [[nodiscard]] Capacities capacities() const override
    {
        return Capacities::ZEROING | Capacities::SAMPLE_STREAM | Capacities::LOGGING;
    }
    size_t finger_size =0;
private:
    uint8_t slave_=0;
    std::atomic<bool> running_{};
    std::jthread      thread_{};
    std::atomic<bool> wait_ = true;
};

#endif //! SCANNER_DHS_MASTER_DEVICE_H