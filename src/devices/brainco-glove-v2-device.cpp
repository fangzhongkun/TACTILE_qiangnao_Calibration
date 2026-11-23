#include "brainco-glove-v2-device.h"

#include "logging.h"

#include <array>
#include <algorithm>
#include <cstring>

namespace
{
    constexpr uint8_t kChannelFrames = 0x02U;

    struct RegionGeometry
    {
        uint8_t y_start;
        uint8_t y_count;
    };

    constexpr std::array<uint8_t, 5> kRows{ 11U, 11U, 11U, 11U, 9U };
    constexpr std::array<std::array<uint8_t, 11>, 5> kPointMasks{{
        { 0x18U, 0x18U, 0x91U, 0x91U, 0x11U, 0xD3U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU },
        { 0x0CU, 0x0CU, 0x08U, 0x49U, 0x49U, 0x48U, 0xFFU, 0x3FU, 0x3FU, 0xFFU, 0xFFU },
        { 0x0CU, 0x0CU, 0x08U, 0x49U, 0x49U, 0x48U, 0xFFU, 0x3FU, 0x3FU, 0xFFU, 0xFFU },
        { 0x0CU, 0x0CU, 0x08U, 0x49U, 0x49U, 0x48U, 0xFFU, 0x3FU, 0x3FU, 0xFFU, 0xFFU },
        { 0x0CU, 0x08U, 0x29U, 0x29U, 0x28U, 0xFFU, 0x3FU, 0x3FU, 0x3FU, 0x00U, 0x00U },
    }};

    bool regionGeometry(uint8_t part, uint8_t region, RegionGeometry& out)
    {
        if (part == 1U) {
            if (region == 1U) out = { 0U, 8U };
            else if (region == 2U) out = { 8U, 3U };
            else return false;
            return true;
        }
        if (part >= 2U && part <= 4U) {
            if (region == 1U) out = { 0U, 7U };
            else if (region == 2U) out = { 7U, 2U };
            else if (region == 3U) out = { 9U, 2U };
            else return false;
            return true;
        }
        if (part == 5U) {
            if (region == 1U) out = { 0U, 6U };
            else if (region == 2U) out = { 6U, 1U };
            else if (region == 3U) out = { 7U, 2U };
            else return false;
            return true;
        }
        return false;
    }
}

void BrainCoGloveV2Device::set_id(uint8_t id)
{
    const uint8_t part = static_cast<uint8_t>(id >> 4U);
    if (part >= 1U && part <= 5U)
        selected_part_id_.store(static_cast<uint8_t>((part << 4U) | 0x01U));
}

int BrainCoGloveV2Device::open(int)
{
    prepare();
    whisper_->onerror = [](const auto& msg) { loge("{}", msg); };
    whisper_->on(kChannelFrames, [this](whisper::payload& payload) -> whisper::payload {
        if (payload.remain_size() < 1U) return {};
        const uint8_t descriptor = payload.read<uint8_t>();
        const uint8_t part = static_cast<uint8_t>(descriptor >> 4U);
        const uint8_t region_count = static_cast<uint8_t>(descriptor & 0x0FU);
        if (part < 1U || part > 5U || region_count == 0U || region_count > 3U) return {};

        const uint8_t protocol_id = static_cast<uint8_t>((part << 4U) | 0x01U);
        const uint8_t selected = selected_part_id_.load();
        size_t finger_points = 0U;
        uint8_t seen_regions = 0U;
        touch::matrix<uint16_t> part_frame(kRows[part - 1U], 8, uint16_t{});
        for (uint8_t index = 0U; index < region_count; ++index) {
            if (payload.remain_size() < 3U) return {};
            const uint8_t region = payload.read<uint8_t>();
            const uint8_t point_count = payload.read<uint8_t>();
            const uint8_t bytes_per_point = payload.read<uint8_t>();
            if ((bytes_per_point != 1U && bytes_per_point != 2U) || point_count == 0U ||
                static_cast<size_t>(point_count) * bytes_per_point > payload.remain_size()) return {};
            const uint8_t *raw = payload.read(static_cast<size_t>(point_count) * bytes_per_point);
            finger_points += point_count;

            RegionGeometry geometry{};
            if (!regionGeometry(part, region, geometry) || region > region_count ||
                (seen_regions & static_cast<uint8_t>(1U << (region - 1U))) != 0U)
                return {};
            seen_regions |= static_cast<uint8_t>(1U << (region - 1U));
            size_t expected = 0U;
            for (uint8_t y = geometry.y_start; y < geometry.y_start + geometry.y_count; ++y)
                for (uint8_t x = 0U; x < 8U; ++x)
                    if ((kPointMasks[part - 1U][y] & static_cast<uint8_t>(1U << x)) != 0U) ++expected;
            if (expected != point_count) {
                logw("BrainCo region length mismatch: part={}, region={}, packet={}, expected={}", part,
                     region, point_count, expected);
                return {};
            }

            size_t raw_index = 0U;
            for (uint8_t y = geometry.y_start; y < geometry.y_start + geometry.y_count; ++y)
                for (uint8_t x = 0U; x < 8U; ++x)
                    if ((kPointMasks[part - 1U][y] & static_cast<uint8_t>(1U << x)) != 0U)
                    {
                        const size_t byte_index = static_cast<size_t>(raw_index) * bytes_per_point;
                        part_frame(y, x) = static_cast<uint16_t>(raw[byte_index]);
                        if (bytes_per_point == 2U)
                            part_frame(y, x) |= static_cast<uint16_t>(raw[byte_index + 1U]) << 8U;
                        ++raw_index;
                    }
        }
        const uint8_t expected_regions = static_cast<uint8_t>((1U << region_count) - 1U);
        if (payload.remain_size() != 0U || seen_regions != expected_regions) {
            logw("BrainCo frame has {} trailing byte(s): descriptor=0x{:02X}", payload.remain_size(), descriptor);
            return {};
        }
        finger_size_.store(finger_points);
        if (protocol_id == selected && onarrived)
            onarrived(protocol_id, part_frame);
        return {};
    }, true);
    return serial_->open(options_);
}

void BrainCoGloveV2Device::set_run(bool run)
{
    if (serial_ && serial_->ready()) enable(Capacities::SAMPLE_STREAM, run);
}

std::string BrainCoGloveV2Device::serial_number() const
{
    if (!serial_ || !serial_->ready()) return {};
    char value[33]{};
    whisper_->get(0x01U, whisper::payload{ 0x05U },
                  [&](whisper::payload& payload) {
                      const size_t size = std::min<size_t>(32U, payload.remain_size());
                      if (size != 0U) std::memcpy(value, payload.read(size), size);
                  },
                  [](const std::string&) { loge("failed to query BrainCo serial number"); });
    return std::string(value);
}

std::string BrainCoGloveV2Device::firmware_version() const
{
    if (!serial_ || !serial_->ready()) return {};
    std::string value;
    whisper_->get(0x01U, whisper::payload{ 0x01U },
                  [&](whisper::payload& payload) {
                      const char *text = reinterpret_cast<const char *>(payload.remain());
                      const size_t size = payload.remain_size();
                      value.assign(text, std::find(text, text + size, '\0'));
                  },
                  [](const std::string&) { loge("failed to query BrainCo firmware version"); });
    return value;
}

uint8_t BrainCoGloveV2Device::address() const
{
    if (!serial_ || !serial_->ready()) return 0xffU;
    uint8_t value = 0xffU;
    whisper_->get(0x01U, whisper::payload{ 0x06U },
                  [&](whisper::payload& payload) {
                      if (payload.remain_size() >= 1U) value = payload.read<uint8_t>();
                  },
                  [](const std::string&) { loge("failed to query BrainCo address"); });
    return value;
}

uint8_t BrainCoGloveV2Device::calibration_query() const
{
    if (!serial_ || !serial_->ready()) return 0U;
    uint8_t value = 0U;
    whisper_->get(0x01U, whisper::payload{ 0x02U },
                  [&](whisper::payload& payload) {
                      if (payload.remain_size() >= 1U) value = payload.read<uint8_t>();
                  },
                  [](const std::string&) { loge("failed to query BrainCo pressure mode"); });
    return value;
}
