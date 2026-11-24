#ifndef SCANNER_DEVICE_H
#define SCANNER_DEVICE_H

#include "calibration_protocol.h"
#include "libtouch/gesture.h"
#include "libtouch/matrix.h"
#include "libtouch/zeroing.h"
#include "sensor.h"
#include "serial/options.h"
#include "serial/serial.h"

#include <cstdint>
#include <functional>
#include <map>
#include <probe/enum.h>
#include <probe/util.h>
#include <utility>
#include <whisper/whisper.h>

enum class Capacities : uint64_t
{
    NONE                = 0x00,
    ZEROING             = 0x01 << 0,
    LOGGING             = 0x01 << 1,
    GESTURE_RECOGNITION = 0x01 << 2,
    CALIBRATION         = 0x01 << 3,
    SAMPLE_STREAM       = 0x01 << 4,
    SLEEP               = 0x01 << 5,
    LOW_POWER_MODE      = 0x01 << 6,
    OTA                 = 0x01 << 7,

    ENABLE_BITMASK_OPERATORS()
};

enum class PixelFormat : uint8_t
{
    UINT16          = 0b000,
    UINT8           = 0b001,
    INT8            = 0b010,
    INT16           = 0b011,
    INT32           = 0b100,
    UINT32          = 0b101,
    FLOAT32         = 0b110,
    GRAY16_RESERVED = 0b111,
};

struct Device
{
    explicit Device(serial::options options) : options_(std::move(options)) {}
    Device(const Device&)            = delete;
    Device& operator=(const Device&) = delete;
    void    set_slave(uint8_t id) { slave_ = id; }
    virtual ~Device() = default;
    virtual int                open() { return -1; }
    virtual int                open(const int dev) { return -1; }
    virtual size_t             get_finger_size() = 0;
    virtual void               set_run(bool run) {};
    virtual uint8_t            get_id() = 0;
    virtual void               set_id(uint8_t id) {};
    virtual uint8_t            calibration_query() const;
    // 下发 float 类型 LUT 包。count 为数组元素个数。
    virtual void               calibration_curve(float *buf, size_t count, int package_id);
    virtual void               re_calibration();
    CalibrationDownloadResult  download_calibration_lut(uint8_t part_id, const QVector<float>& adc,
                                                        const QVector<float>& force_cn, uint32_t version,
                                                        uint8_t region_id = 1U);
    CalibrationDownloadResult  query_calibration_status(uint8_t part_id, uint8_t region_id = 1U);
    // 一次查询 14 个区域的标定状态（STATUS_ALL 0x03）。
    CalibrationRegionMaskResult query_calibration_all();
    // region_id 为 0 时擦除整根手指；1～3 时只擦除该区域（ERASE 带 Scope=0x01）。
    CalibrationDownloadResult  erase_calibration_lut(uint8_t part_id, uint8_t region_id = 0U);
    CalibrationDownloadResult  set_calibration_mode(uint8_t part_id, uint8_t mode);
    PartMetadataResult         query_part_metadata(uint8_t part_id) const;
    [[nodiscard]] virtual bool ready() const { return serial_ && serial_->ready(); }

    [[nodiscard]] virtual std::string name() const { return options_.port; }

    [[nodiscard]] virtual probe::version_t version() const;

    [[nodiscard]] virtual std::string      bus() const { return "serial"; }
    [[nodiscard]] virtual probe::version_t bus_version() const;

    [[nodiscard]] virtual std::map<int /*id*/, Sensor> sensors() const;

    [[nodiscard]] virtual std::pair<int32_t, int32_t> range() const;

    [[nodiscard]] virtual std::string serial_number() const;

    [[nodiscard]] virtual std::string firmware_version() const;

    [[nodiscard]] virtual uint8_t address() const;

    virtual int address(uint8_t);

    // capacities
    [[nodiscard]] virtual Capacities capacities() const { return Capacities::NONE; }

    virtual int enable(Capacities, bool en);

    virtual void stop() {}

    virtual void start() {};

    virtual void pause() {}

    virtual void resume() {}

    // zeroing
    virtual int zeroing(int /*MCU or COMPUTER*/, const touch::zeroing& /*zeroing*/);

    virtual int rezeroing();
    // calibration
    //  TODO: calibration
    virtual int calibration() { return 0; }

    virtual int sleep(bool) { return 0; }

    [[nodiscard]] std::shared_ptr<whisper::whisper> whisper() const { return whisper_; }

    //

    void send(uint8_t channel, const whisper::payload& payload) { whisper_->put(channel, payload); }

    // frame arrived
    std::function<void(uint32_t id, const touch::matrix<uint16_t>&)> onarrived = [](auto&&...) {};

    // OP3/AW86872 bridge data arrived: positive channel and negative channel from the second logical
    // packet.
    std::function<void(uint16_t positive, uint16_t negative)> onbridgearrived = [](auto&&...) {};

    // gesture
    std::function<void(uint8_t id, touch::gesture_t, uint32_t N, int32_t x, int32_t y, int32_t speed)>
        ongesture = [](auto&&...) {};

    // metainfo
    std::function<void()> onmetachanged = [] {};

    std::function<void()> ondisconnected = [] {};

    template<class FMT> touch::matrix<FMT> parse_frame(whisper::payload& payload)
    {
        const auto cols = payload.read<uint8_t>();
        const auto rows = payload.read<uint8_t>();
        const auto size = rows * cols * sizeof(FMT);

        if (!size || size > payload.remain_size()) {
            return {};
        }
        return touch::matrix<FMT>(rows, cols, payload.read(size));
    }

protected:
    void
    prepare(const std::shared_ptr<whisper::packager>& pkger = std::make_shared<whisper::default_packager>(),
            bool                                      logging = true);

protected:
    std::shared_ptr<whisper::whisper>    whisper_{};
    std::unique_ptr<serial::serial_port> serial_{}; // ATTENTION: destruct before whisper

    serial::options options_{};

    uint8_t slave_ = 0;
};

#endif // !SCANNER_DEVICE_H
