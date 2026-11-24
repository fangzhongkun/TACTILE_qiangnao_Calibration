#include "dhs-default-device.h"

#include "logging.h"
constexpr uint8_t CHANNEL_METAINFO = 0x01;
constexpr uint8_t CHANNEL_FRAMES   = 0x02;
constexpr uint8_t CHANNEL_LOGGING  = 0x05;
constexpr uint8_t CHANNEL_CONTROL  = 0x06;
constexpr uint8_t CHANNEL_ZEROING  = 0x07;

int DhsDefaultDevice::open(int dev)
{
    prepare();
    whisper_->onerror = [](const auto& msg) { loge("{}", msg); };

    // F103 聚合上传格式：
    //   finger: [part|region_count] ([region_id rows cols] [u8 data])...
    //   palm:   [0x61 0x01 count_le16] [count bytes]
    // UI 只接收当前选择的部位（slave_ 为 0x11..0x61）。
    whisper_->on(CHANNEL_FRAMES, [=, this](whisper::payload& payload) -> whisper::payload {
        static uint32_t rx_count = 0U;
        ++rx_count;
        if (payload.remain_size() < 1U) return {};
        const uint8_t descriptor  = payload.read<uint8_t>();
        const uint8_t part        = static_cast<uint8_t>(descriptor >> 4U);
        const uint8_t protocol_id = static_cast<uint8_t>((part << 4U) | 0x01U);
        if ((rx_count % 500U) == 1U)
            logi("aggregate upload: descriptor=0x{:02X}, part={}, remain={}, selected=0x{:02X}", descriptor,
                 part, payload.remain_size(), selected_protocol_id_.load());
        if (part < 1U || part > 6U) return {};

        touch::matrix<uint16_t> frame;
        size_t                  total_size = 0U;
        if (part == 6U) {
            if (payload.remain_size() < 3U) return {};
            (void)payload.read<uint8_t>(); // compact format version
            const uint16_t count = payload.read<uint16_t>();
            if (count == 0U || count > payload.remain_size()) return {};
            const auto *raw = payload.read(count);
            frame           = touch::matrix<uint16_t>(1, count, uint16_t{});
            for (uint16_t i = 0U; i < count; ++i) frame(0, i) = raw[i];
            total_size = count;
        }
        else {
            const uint8_t region_count = static_cast<uint8_t>(descriptor & 0x0FU);
            if (region_count == 0U || region_count > 8U) return {};
            struct Region
            {
                uint8_t        rows;
                uint8_t        cols;
                const uint8_t *raw;
            };
            Region regions[8]{};
            int    common_rows = 0;
            int    total_cols  = 0;
            for (uint8_t i = 0U; i < region_count; ++i) {
                if (payload.remain_size() < 3U) return {};
                (void)payload.read<uint8_t>(); // region id
                const uint8_t rows  = payload.read<uint8_t>();
                const uint8_t cols  = payload.read<uint8_t>();
                const size_t  bytes = static_cast<size_t>(rows) * cols;
                if (rows == 0U || cols == 0U || bytes > payload.remain_size() ||
                    (common_rows != 0 && common_rows != rows))
                    return {};
                regions[i]  = Region{ rows, cols, payload.read(bytes) };
                common_rows = rows;
                total_cols += cols;
                total_size += bytes;
            }
            frame        = touch::matrix<uint16_t>(common_rows, total_cols, uint16_t{});
            int col_base = 0;
            for (uint8_t i = 0U; i < region_count; ++i) {
                for (int c = 0; c < regions[i].cols; ++c)
                    for (int r = 0; r < regions[i].rows; ++r)
                        frame(r, col_base + c) = regions[i].raw[c * regions[i].rows + r];
                col_base += regions[i].cols;
            }
        }
        finger_size = total_size;
        if (selected_protocol_id_.load() == protocol_id && onarrived) onarrived(protocol_id, frame);
        return {};
    });
    ///
    return serial_->open(options_);
}

void DhsDefaultDevice::set_run(bool run)
{
    // DhsDefaultDevice 使用下位机主动上传模式；暂停/恢复时通过控制通道开关采样流，
    // 避免标定 LUT 大包下发期间主动上传帧抢占串口。
    if (!serial_ || !serial_->ready()) {
        return;
    }
    enable(Capacities::SAMPLE_STREAM, run);
}
