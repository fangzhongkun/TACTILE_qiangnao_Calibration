#include "haocun-glove-device.h"
#include "logging.h"
#include <algorithm>
#include <cstring>

void HaocunGloveDevice::set_id(uint8_t id)
{
    const uint8_t part = static_cast<uint8_t>(id >> 4U);
    if (part >= 1U && part <= 6U) selected_part_id_.store(static_cast<uint8_t>((part << 4U) | 1U));
}

int HaocunGloveDevice::open(int)
{
    prepare();
    whisper_->onerror = [](const auto& msg) { loge("{}", msg); };
    whisper_->on(0x02U, [this](whisper::payload& payload) -> whisper::payload {
        if (payload.remain_size() < 4U) return {};
        const uint8_t id = payload.read<uint8_t>();
        const uint8_t format = payload.read<uint8_t>();
        const uint8_t rows = payload.read<uint8_t>();
        const uint8_t cols = payload.read<uint8_t>();
        const uint8_t part = static_cast<uint8_t>(id >> 4U);
        if (part < 1U || part > 6U || (id & 0x0FU) != 1U || format != 1U || rows == 0U || cols == 0U)
            return {};
        const size_t points = static_cast<size_t>(rows) * cols;
        if (payload.remain_size() != points) return {};
        touch::matrix<uint16_t> frame(rows, cols, uint16_t{});
        const uint8_t *raw = payload.read(points);
        // MCU 按 x 外层、y 内层打包；matrix 使用 row(y), col(x)。
        for (uint8_t x = 0U; x < cols; ++x)
            for (uint8_t y = 0U; y < rows; ++y)
                frame(y, x) = raw[static_cast<size_t>(x) * rows + y];
        point_count_.store(points);
        if (selected_part_id_.load() == id && onarrived) onarrived(id, frame);
        return {};
    });
    return serial_->open(options_);
}

void HaocunGloveDevice::set_run(bool run)
{
    if (serial_ && serial_->ready()) enable(Capacities::SAMPLE_STREAM, run);
}

std::string HaocunGloveDevice::serial_number() const
{
    if (!ready()) return {};
    char value[33]{};
    whisper_->get(0x01U, whisper::payload{0x05U}, [&](whisper::payload& p) {
        const size_t n = std::min<size_t>(32U, p.remain_size());
        if (n) std::memcpy(value, p.read(n), n);
    }, [](const std::string&) { loge("failed to query Haocun serial number"); });
    return value;
}

std::string HaocunGloveDevice::firmware_version() const
{
    if (!ready()) return {};
    std::string value;
    whisper_->get(0x01U, whisper::payload{0x01U}, [&](whisper::payload& p) {
        const char *s = reinterpret_cast<const char *>(p.remain());
        value.assign(s, std::find(s, s + p.remain_size(), '\0'));
    }, [](const std::string&) { loge("failed to query Haocun firmware version"); });
    return value;
}

uint8_t HaocunGloveDevice::address() const
{
    if (!ready()) return 0xffU;
    uint8_t value = 0xffU;
    whisper_->get(0x01U, whisper::payload{0x06U}, [&](whisper::payload& p) {
        if (p.remain_size()) value = p.read<uint8_t>();
    }, [](const std::string&) { loge("failed to query Haocun address"); });
    return value;
}

uint8_t HaocunGloveDevice::calibration_query() const
{
    if (!ready()) return 0U;
    uint8_t value = 0U;
    whisper_->get(0x01U, whisper::payload{0x02U}, [&](whisper::payload& p) {
        if (p.remain_size()) value = p.read<uint8_t>();
    }, [](const std::string&) { loge("failed to query Haocun pressure mode"); });
    return value;
}
