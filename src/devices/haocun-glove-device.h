#ifndef SCANNER_HAOCUN_GLOVE_DEVICE_H
#define SCANNER_HAOCUN_GLOVE_DEVICE_H

#include "device.h"
#include <atomic>

// 灏存手套由一颗 MCU 连续上报 6 个独立矩形：五指和掌心。
struct HaocunGloveDevice final : public Device
{
    explicit HaocunGloveDevice(serial::options options) : Device(std::move(options)) { set_slave(0); }
    int open(int dev) override;
    size_t get_finger_size() override { return point_count_.load(); }
    uint8_t get_id() override { return selected_part_id_.load(); }
    void set_id(uint8_t id) override;
    void set_run(bool run) override;
    [[nodiscard]] std::string serial_number() const override;
    [[nodiscard]] std::string firmware_version() const override;
    [[nodiscard]] uint8_t address() const override;
    [[nodiscard]] uint8_t calibration_query() const override;
    [[nodiscard]] std::string bus() const override { return "Whisper / Haocun Glove"; }
    [[nodiscard]] std::pair<int32_t, int32_t> range() const override { return {0, 255}; }
    [[nodiscard]] Capacities capacities() const override
    { return Capacities::ZEROING | Capacities::SAMPLE_STREAM | Capacities::CALIBRATION; }

private:
    std::atomic<uint8_t> selected_part_id_{0x11U};
    std::atomic<size_t> point_count_{0U};
};

#endif
