#ifndef SCANNER_BRAINCO_GLOVE_V2_DEVICE_H
#define SCANNER_BRAINCO_GLOVE_V2_DEVICE_H

#include "device.h"

#include <atomic>

// BrainCo Glove V2 is a single-MCU device. The selected ID is the logical
// part ID 0x11..0x51. All region blocks in one upload are combined per part.
struct BrainCoGloveV2Device final : public Device
{
    explicit BrainCoGloveV2Device(serial::options options) : Device(std::move(options)) { set_slave(0); }

    int     open(int dev) override;
    size_t  get_finger_size() override { return finger_size_; }
    uint8_t get_id() override { return selected_part_id_.load(); }
    void    set_id(uint8_t id) override;
    void    set_run(bool run) override;
    [[nodiscard]] std::string serial_number() const override;
    [[nodiscard]] std::string firmware_version() const override;
    [[nodiscard]] uint8_t address() const override;
    [[nodiscard]] uint8_t calibration_query() const override;

    [[nodiscard]] std::string bus() const override { return "Whisper / BrainCo Glove V2"; }
    // Display scale for the 12-bit raw AD samples; uint16 wire decoding remains unchanged.
    [[nodiscard]] std::pair<int32_t, int32_t> range() const override { return { 0, 4095 }; }
    [[nodiscard]] Capacities capacities() const override
    {
        return Capacities::ZEROING | Capacities::SAMPLE_STREAM | Capacities::CALIBRATION;
    }

private:
    std::atomic<uint8_t> selected_part_id_{ 0x11 };
    std::atomic<size_t>  finger_size_{ 0U };
};

#endif
