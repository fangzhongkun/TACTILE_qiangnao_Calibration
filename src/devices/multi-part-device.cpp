#include "multi-part-device.h"

#include "logging.h"

#include <chrono>
#include <thread>

constexpr uint8_t CHANNEL_METAINFO = 0x01;
constexpr uint8_t CHANNEL_FRAMES   = 0x02;
constexpr uint8_t CHANNEL_CONTROL  = 0x06;
constexpr uint8_t CHANNEL_ZEROING  = 0x07;

MultiPartDevice::MultiPartDevice(serial::options options) : Device(std::move(options))
{
    logi("MultiPartDevice created");
}

int MultiPartDevice::open()
{
    prepare();

    whisper_->onerror = [](const auto& err) { loge("Whisper error: {}", err); };

    if (serial_->open(options_) != 0) {
        loge("Failed to open serial port: {}", options_.port);
        return -1;
    }

    logi("Serial port opened: {}", options_.port);

    // 扫描所有部位
    auto discovered_parts = scan_parts();
    logi("Discovered {} parts", discovered_parts.size());

    return serial_->ready() ? 0 : -1;
}

void MultiPartDevice::set_active_part(uint8_t part_id)
{
    if (part_id < 1 || part_id > 5) {
        logw("Invalid part_id: {}, valid range is 1-5", part_id);
        return;
    }

    active_part_ = part_id;
    logi("Active part set to: 0x{:02X}", active_part_);
}

std::vector<uint8_t> MultiPartDevice::scan_parts()
{
    std::vector<uint8_t> found_parts;

    if (!serial_ || !serial_->ready()) {
        loge("Serial not ready for scanning");
        return found_parts;
    }

    logi("Scanning for connected parts...");

    // 扫描地址 0x01 ~ 0x05（五指）
    for (uint8_t part_id = 0x01; part_id <= 0x05; ++part_id) {
        uint8_t channel = (part_id << 4) | CHANNEL_METAINFO;

        bool part_found = false;
        whisper_->get(
            channel, whisper::payload{ 0x01 }, // 查询版本
            [&](whisper::payload& payload) {
                part_found                 = true;
                parts_discovered_[part_id] = true;
                logi("Found part 0x{:02X}", part_id);
            },
            [part_id](const std::string& err) { logw("Part 0x{:02X} not found: {}", part_id, err); });

        if (part_found) {
            found_parts.push_back(part_id);
        }

        // 短暂延迟避免总线冲突
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    if (found_parts.empty()) {
        logw("No parts found on the bus");
    }

    return found_parts;
}

void MultiPartDevice::calibration_curve_for_part(uint8_t part_id, float *buf, size_t count, int package_id)
{
    if (!buf || count == 0U) {
        logw("calibration_curve_for_part skipped: empty buffer, part=0x{:02X}, package_id={}", part_id,
             package_id);
        return;
    }

    if (package_id < 0 || package_id > 4) {
        loge("calibration_curve_for_part failed: invalid package_id={} for part 0x{:02X}", package_id,
             part_id);
        return;
    }

    if (count > 0xFFU) {
        loge("calibration_curve_for_part failed: count={} exceeds protocol limit for part 0x{:02X}", count,
             part_id);
        return;
    }

    // 构造通道地址：(part_id << 4) | CHANNEL_ZEROING
    uint8_t channel = (part_id << 4) | CHANNEL_ZEROING;

    // 构造命令码：(package_id << 4) | 0x03
    uint8_t cmd = static_cast<uint8_t>(((package_id & 0x0F) << 4) | 0x03);

    whisper::payload payload{};
    payload.write<uint8_t>(cmd);
    payload.write<uint8_t>(static_cast<uint8_t>(count));
    payload.write(buf, sizeof(float) * count);

    whisper_->put(channel, payload);

    logi("Sent calibration curve to part 0x{:02X}, package_id={}, count={}", part_id, package_id, count);
}

uint8_t MultiPartDevice::calibration_query_for_part(uint8_t part_id)
{
    if (!serial_ || !serial_->ready()) {
        logw("Serial not ready for calibration query, part 0x{:02X}", part_id);
        return 0;
    }

    uint8_t channel = (part_id << 4) | CHANNEL_METAINFO;
    uint8_t status  = 0;

    whisper_->get(
        channel, whisper::payload{ 0x02 }, // 查询标定状态
        [&](whisper::payload& payload) { status = payload.read<uint8_t>(); },
        [part_id](const std::string& err) {
            loge("Failed to query calibration status for part 0x{:02X}: {}", part_id, err);
        });

    return status;
}

int MultiPartDevice::zeroing_for_part(uint8_t part_id, const touch::zeroing& zeroing)
{
    if (!serial_ || !serial_->ready()) {
        loge("Serial not ready for zeroing, part 0x{:02X}", part_id);
        return -1;
    }

    uint8_t channel = (part_id << 4) | CHANNEL_ZEROING;

    whisper::payload payload{};
    payload
        .write<uint8_t>(0x02) // CMD: ZEROING_PARAMS
        .write(zeroing.method)
        .write(zeroing.frames)
        .write(zeroing.alpha)
        .write(zeroing.static_beta)
        .write(zeroing.dynamic_beta)
        .write(touch::threshold_t::TOZERO);

    whisper_->put(channel, payload);

    logi("Sent zeroing params to part 0x{:02X}: method={}, frames={}, alpha={}", part_id,
         static_cast<int>(zeroing.method), zeroing.frames, zeroing.alpha);

    return 0;
}

int MultiPartDevice::enable_zeroing_for_part(uint8_t part_id, bool enable)
{
    if (!serial_ || !serial_->ready()) {
        loge("Serial not ready for enabling zeroing, part 0x{:02X}", part_id);
        return -1;
    }

    uint8_t channel = (part_id << 4) | CHANNEL_ZEROING;

    whisper_->put(channel, whisper::payload{ 0x01, enable ? uint8_t(0x01) : uint8_t(0x00) });

    logi("Zeroing {} for part 0x{:02X}", enable ? "enabled" : "disabled", part_id);

    return 0;
}

std::map<uint8_t, touch::matrix<uint16_t>> MultiPartDevice::read_all_parts_frames()
{
    std::map<uint8_t, touch::matrix<uint16_t>> frames;

    if (!serial_ || !serial_->ready()) {
        return frames;
    }

    uint32_t now = 0; // TODO: 使用实际时间戳

    // 读取所有已发现部位的数据
    for (const auto& [part_id, discovered] : parts_discovered_) {
        if (!discovered) continue;

        uint8_t channel = (part_id << 4) | CHANNEL_FRAMES;

        whisper_->get(
            channel, whisper::payload{ 0x01 }, // 请求数据帧
            [&](whisper::payload& payload) {
                auto frame = parse_frame<uint16_t>(payload);
                if (!frame.empty()) {
                    frames[part_id]           = frame;
                    latest_frames_[part_id]   = frame;
                    last_frame_tick_[part_id] = now;

                    // 触发回调
                    if (onpart_arrived) {
                        onpart_arrived(part_id, frame);
                    }
                }
            },
            [part_id](const std::string& err) {
                logw("Failed to read frame from part 0x{:02X}: {}", part_id, err);
            });
    }

    return frames;
}

std::map<uint8_t, bool> MultiPartDevice::get_parts_online_status()
{
    std::map<uint8_t, bool> status;

    uint32_t now = 0; // TODO: 使用实际时间戳

    for (const auto& [part_id, discovered] : parts_discovered_) {
        if (!discovered) {
            status[part_id] = false;
            continue;
        }

        auto it = last_frame_tick_.find(part_id);
        if (it == last_frame_tick_.end()) {
            status[part_id] = false;
        }
        else {
            uint32_t elapsed = now - it->second;
            status[part_id]  = (elapsed < FRAME_TIMEOUT_MS);
        }
    }

    return status;
}
