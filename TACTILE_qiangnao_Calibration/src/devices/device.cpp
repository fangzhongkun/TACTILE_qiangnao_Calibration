#include "device.h"

#include "logging.h"
#include "QDebug"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <thread>
#include <QByteArray>
#include <QDateTime>
constexpr uint8_t CHANNEL_METAINFO = 0x01;
constexpr uint8_t CHANNEL_FRAMES   = 0x02;
constexpr uint8_t CHANNEL_LOGGING  = 0x05;
constexpr uint8_t CHANNEL_CONTROL  = 0x06;
constexpr uint8_t CHANNEL_ZEROING  = 0x07;

static uint8_t part_channel(uint8_t slave, uint8_t channel)
{
    const uint8_t part = static_cast<uint8_t>(slave >> 4U);
    if ((part >= 1U) && (part <= 6U) && ((slave & 0x0FU) == CHANNEL_METAINFO))
        return static_cast<uint8_t>((part << 4U) | channel);
    return static_cast<uint8_t>((slave << 4U) | channel);
}
void Device::prepare(const std::shared_ptr<whisper::packager>& pkger, bool logging)
{
    whisper_ = std::make_unique<whisper::whisper>(pkger);
    serial_  = std::make_unique<serial::serial_port>();

    serial_->rx  = [&](auto data, size_t size) { whisper_->rx(data, size); };
    whisper_->tx = [&](auto data, size_t size) { serial_->tx(data, size); };

    serial_->onaborted = [this]() { ondisconnected(); };

    if (logging) {
        whisper_->on(CHANNEL_LOGGING, [](whisper::payload& payload) -> whisper::payload {
            const auto        level = payload.read<uint8_t>();
            const std::string msg(reinterpret_cast<const char *>(payload.remain()), payload.remain_size());
            switch (level) {
            case 1:  logi("(DEVICE) {}", msg); break;
            case 2:  logw("(DEVICE) {}", msg); break;
            case 3:  loge("(DEVICE) {}", msg); break;
            default: logd("(DEVICE) {}", msg); break;
            }
            return {};
        });
    }
}

probe::version_t Device::version() const
{
    if (!serial_ || !serial_->ready()) return {};

    probe::version_t version{};

    whisper_->get(
        CHANNEL_METAINFO, whisper::payload{ 0x01 },
        [&](whisper::payload& payload) {
            version = probe::version_t{
                .major = payload.read<uint8_t>(),
                .minor = payload.read<uint8_t>(),
                .patch = payload.read<uint32_t>(),
            };
        },
        [](const std::string&) { LOG(ERROR) << "failed to query the device version"; });

    return version;
}

probe::version_t Device::bus_version() const
{
    if (!serial_ || !serial_->ready()) return {};

    probe::version_t version{};

    whisper_->get(
        CHANNEL_METAINFO, whisper::payload{ 0x02 },
        [&](whisper::payload& payload) {
            version = probe::version_t{
                .major = payload.read<uint8_t>(),
                .minor = payload.read<uint8_t>(),
                .patch = payload.read<uint32_t>(),
            };
        },
        [](const std::string&) { LOG(ERROR) << "failed to query the whisper version"; });

    return version;
}

std::map<int, Sensor> Device::sensors() const
{
    if (!serial_ || !serial_->ready()) return {};

    std::map<int, Sensor> sensors{};

    whisper_->get(
        CHANNEL_METAINFO, whisper::payload{ 0x03 },
        [&](whisper::payload& payload) {
            const auto number = payload.read<uint8_t>();
            for (uint8_t i = 0; i < number; ++i) {
                sensors[i] = {
                    .width           = payload.read<uint16_t>(),
                    .height          = payload.read<uint16_t>(),
                    .format          = payload.read<uint8_t>(),
                    .physical_width  = payload.read<uint16_t>(),
                    .physical_height = payload.read<uint16_t>(),
                    .sar             = {
                        .num = payload.read<uint8_t>(),
                        .den = payload.read<uint8_t>(),
                    },
                };
            }
        },
        [](const std::string&) { loge("failed to query the sensor properties"); });

    return sensors;
}

std::pair<int32_t, int32_t> Device::range() const
{
    if (!serial_ || !serial_->ready()) return {};

    std::pair<int32_t, int32_t> range{ 0, 4095 };

    whisper_->get(
        CHANNEL_METAINFO, whisper::payload{ 0x04 },
        [&](whisper::payload& payload) {
            range.first  = payload.read<int32_t>();
            range.second = payload.read<int32_t>();
        },
        [](const std::string&) { loge("failed to query the data range, default: 0 ~ 4095"); });

    return range;
}

std::string Device::serial_number() const
{
    if (!serial_ || !serial_->ready()) return {};

    const uint8_t part = static_cast<uint8_t>(slave_ >> 4U);
    if ((part >= 1U) && (part <= 6U) && ((slave_ & 0x0FU) == CHANNEL_METAINFO)) {
        const auto metadata = query_part_metadata(part);
        return metadata.success ? metadata.serial_number : std::string{ "(Unknown)" };
    }

    char buf[32]{ "(Unknown)" };
    whisper_->get(
        part_channel(slave_, CHANNEL_METAINFO), whisper::payload{ 0x05 },
        [&](whisper::payload& payload) { std::memcpy(buf, payload.read(32), 32); },
        [](const std::string&) { loge("failed to query the serial number"); });

    return std::string{ buf };
}

std::string Device::firmware_version() const
{
    if (!serial_ || !serial_->ready()) return {};

    const uint8_t part = static_cast<uint8_t>(slave_ >> 4U);
    if ((part >= 1U) && (part <= 6U) && ((slave_ & 0x0FU) == CHANNEL_METAINFO)) {
        const auto metadata = query_part_metadata(part);
        return metadata.success ? metadata.firmware : std::string{};
    }

    char buf[14]{};
    whisper_->get(
        part_channel(slave_, CHANNEL_METAINFO), whisper::payload{ 0x01 },
        [&](whisper::payload& payload) { std::memcpy(buf, payload.read(14), 14); },
        [](const std::string&) { loge("failed to query the serial number"); });

    return std::string{ buf };
}

uint8_t Device::address() const
{
    if (!serial_ || !serial_->ready()) return {};
    const uint8_t part = static_cast<uint8_t>(slave_ >> 4U);
    if ((part >= 1U) && (part <= 6U) && ((slave_ & 0x0FU) == CHANNEL_METAINFO)) {
        const auto metadata = query_part_metadata(part);
        return metadata.success ? metadata.device_id : 0xffU;
    }
    uint8_t addr{};
    whisper_->get(
        part_channel(slave_, CHANNEL_METAINFO), whisper::payload{ 0x06 },
        [&](whisper::payload& payload) { addr = payload.read<uint8_t>(); },
        [](const std::string&) { loge("failed to query the device address"); });

    return addr;
}
uint8_t Device::calibration_query() const
{
    if (!serial_ || !serial_->ready()) return {};
    uint8_t tep{};
    whisper_->get(
        part_channel(slave_, CHANNEL_METAINFO), whisper::payload{ 0x02 },
        [&](whisper::payload& payload) { tep = payload.read<uint8_t>(); },
        [](const std::string&) { loge("failed to query the device address"); });

    return tep;
}

int Device::address(uint8_t addr)
{
    // if (!serial_ || !serial_->ready()) return -1;
    //
    // whisper_->put(0x09, whisper::payload{ addr });
    //
    // return 0;
    if (!serial_ || !serial_->ready()) return -1;

    whisper_->put(0x09, whisper::payload{ 0x04, addr });

    return 0;
}

int Device::enable(const Capacities cap, const bool en)
{
    if (!serial_ || !serial_->ready()) return -1;

    if (!!(cap & Capacities::SAMPLE_STREAM)) {
        whisper_->put(CHANNEL_CONTROL, whisper::payload{ 0x01, en });
    }

    if (!!(cap & Capacities::GESTURE_RECOGNITION)) {
        whisper_->put(CHANNEL_CONTROL, whisper::payload{ 0x03, en });
    }

    if (!!(cap & Capacities::LOGGING)) {
        whisper_->put(CHANNEL_CONTROL, whisper::payload{ 0x04, en });
    }

    if (!!(cap & Capacities::ZEROING)) {
        whisper_->put(CHANNEL_ZEROING, whisper::payload{ 0x01, en });
    }

    return 0;
}

int Device::zeroing(int, const touch::zeroing& zeroing)
{
    if (!serial_ || !serial_->ready()) return -1;

    whisper::payload payload{};
    payload.write<uint8_t>(0x02)
        .write(zeroing.method)
        .write(zeroing.frames)
        .write(zeroing.alpha)
        .write(zeroing.static_beta)
        .write(zeroing.dynamic_beta)

        .write(touch::threshold_t::TOZERO);

    whisper_->put(CHANNEL_ZEROING, payload);
    return 0;
}

int Device::rezeroing()
{
    if (!serial_ || !serial_->ready()) return -1;

    whisper_->put(CHANNEL_ZEROING, whisper::payload{ 0x01, 0x02 });

    return 0;
}

void Device::calibration_curve(float *buf, size_t count, int package_id)
{
    if (!buf || count == 0U) {
        logw("calibration_curve skipped: empty buffer, package_id={}", package_id);
        return;
    }
    if (package_id < 0 || package_id > 4) {
        loge("calibration_curve failed: unsupported package_id={}. Valid range is 0~4", package_id);
        return;
    }
    if (count > 0xffU) {
        // 当前下位机协议的 count 字段为 uint8_t。超过 255 时禁止静默截断，
        // 否则 MCU 会按错误长度解析后续 float 数据。
        loge("calibration_curve failed: count={} exceeds uint8 protocol limit, package_id={}", count,
             package_id);
        return;
    }
    whisper::payload payload{};
    uint8_t          cmd = static_cast<uint8_t>(((package_id & 0x0F) << 4) | 0x03);
    // if (package_id == 1) {
    //     payload.write<uint8_t>(0x03);
    // }
    // else {
    //     payload.write<uint8_t>(cmd);
    // }
    payload.write<uint8_t>(cmd);
    payload.write<uint8_t>(static_cast<uint8_t>(count));
    payload.write(buf, sizeof(float) * count);
    whisper_->put(CHANNEL_ZEROING, payload);
}

void Device::re_calibration()
{
    whisper::payload payload{};
    payload.write<uint8_t>(0x00);
    whisper_->put(0xff, payload);
}

namespace
{
    constexpr uint8_t kCalibrationChannel = 0x07;
    constexpr uint8_t kProtocolVersion    = 1;
    constexpr uint8_t kCmdStatus          = 0x02;
    constexpr uint8_t kCmdStatusAll       = 0x03;
    constexpr uint8_t kEraseScopeRegion   = 0x01;
    constexpr uint8_t kCmdBegin           = 0x10;
    constexpr uint8_t kCmdWrite           = 0x11;
    constexpr uint8_t kCmdVerify          = 0x12;
    constexpr uint8_t kCmdApply           = 0x13;
    constexpr uint8_t kCmdCommit          = 0x14;
    constexpr uint8_t kCmdAbort           = 0x16;
    constexpr uint8_t kCmdErase           = 0x17;
    constexpr uint8_t kCmdSetMode         = 0x18;
    constexpr uint8_t kCmdMetadata        = 0x20;

    uint32_t calibration_crc32(const uint8_t *data, size_t size)
    {
        uint32_t crc = 0xffffffffU;
        while (size-- != 0U) {
            crc ^= *data++;
            for (uint8_t bit = 0; bit < 8U; ++bit)
                crc = (crc & 1U) ? ((crc >> 1U) ^ 0xedb88320U) : (crc >> 1U);
        }
        return crc ^ 0xffffffffU;
    }

    uint32_t read_u32_le(const uint8_t *p)
    {
        return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8U) |
               (static_cast<uint32_t>(p[2]) << 16U) | (static_cast<uint32_t>(p[3]) << 24U);
    }

    CalibrationDownloadResult decode_calibration_response(whisper::payload& payload, uint8_t command,
                                                          uint8_t part, uint16_t sequence, uint16_t session)
    {
        CalibrationDownloadResult result;
        if (payload.size() < 24U) {
            result.error = QStringLiteral("标定响应长度不足");
            return result;
        }
        const uint8_t version          = payload.read<uint8_t>();
        const uint8_t response_command = payload.read<uint8_t>();
        const uint8_t response_part    = payload.read<uint8_t>();
        payload.read<uint8_t>();
        const uint16_t response_sequence = payload.read<uint16_t>();
        const uint16_t response_session  = payload.read<uint16_t>();
        const uint16_t data_len          = payload.read<uint16_t>();
        payload.read<uint16_t>();
        if (version != kProtocolVersion || response_command != static_cast<uint8_t>(command | 0x80U) ||
            response_part != part || response_sequence != sequence || response_session != session ||
            data_len < 12U) {
            result.error = QStringLiteral("标定响应头不匹配");
            return result;
        }
        result.status          = payload.read<uint8_t>();
        result.expected_points = payload.read<uint8_t>();
        result.received_points = payload.read<uint8_t>();
        result.mode            = payload.read<uint8_t>();
        const uint8_t *tail    = payload.read(8U);
        result.crc32           = read_u32_le(tail);
        result.version         = read_u32_le(tail + 4U);
        result.success         = result.status == 0U;
        if (!result.success)
            result.error = QStringLiteral("设备返回标定错误 0x%1")
                               .arg(result.status, 2, 16, QLatin1Char('0'))
                               .toUpper();
        return result;
    }
} // namespace

PartMetadataResult Device::query_part_metadata(uint8_t part_id) const
{
    PartMetadataResult result;
    if (!ready() || part_id < 1U || part_id > 6U) {
        result.error = QStringLiteral("设备未连接或部位编号无效");
        return result;
    }
    static std::atomic<uint16_t> next_sequence{ 0U };
    const uint16_t               sequence = ++next_sequence;
    whisper::payload             request;
    request.write<uint8_t>(kProtocolVersion)
        .write<uint8_t>(kCmdMetadata)
        .write<uint8_t>(part_id)
        .write<uint8_t>(1U)
        .write<uint16_t>(sequence)
        .write<uint16_t>(0U)
        .write<uint16_t>(0U)
        .write<uint16_t>(0U);
    whisper_->get(
        kCalibrationChannel, request,
        [&](whisper::payload& response) {
            if (response.size() < 60U) {
                result.error = QStringLiteral("设备信息响应长度不足");
                return;
            }
            const uint8_t version = response.read<uint8_t>();
            const uint8_t command = response.read<uint8_t>();
            const uint8_t part    = response.read<uint8_t>();
            response.read<uint8_t>();
            const uint16_t response_sequence = response.read<uint16_t>();
            response.read<uint16_t>();
            const uint16_t data_len = response.read<uint16_t>();
            response.read<uint16_t>();
            if (version != kProtocolVersion || command != (kCmdMetadata | 0x80U) || part != part_id ||
                response_sequence != sequence || data_len != 48U) {
                result.error = QStringLiteral("设备信息响应头不匹配");
                return;
            }
            const uint8_t *body = response.read(48U);
            if (body[0] != 0U) {
                result.error = QStringLiteral("设备信息查询失败");
                return;
            }
            result.device_id     = body[1];
            result.firmware      = std::string(reinterpret_cast<const char *>(&body[2]),
                                               strnlen(reinterpret_cast<const char *>(&body[2]), 14U));
            result.serial_number = std::string(reinterpret_cast<const char *>(&body[16]),
                                               strnlen(reinterpret_cast<const char *>(&body[16]), 32U));
            result.success       = !result.firmware.empty() && !result.serial_number.empty();
        },
        [&](const std::string& error) { result.error = QString::fromStdString(error); },
        std::chrono::milliseconds(1500));
    return result;
}

CalibrationDownloadResult Device::query_calibration_status(uint8_t part_id, uint8_t region_id)
{
    CalibrationDownloadResult result;
    if (!ready() || part_id < 1U || part_id > 6U || region_id < 1U || region_id > 6U) {
        result.error = QStringLiteral("设备未连接或部位/区域编号无效");
        return result;
    }
    static std::atomic<uint16_t> next_sequence{ 0U };
    const uint16_t               sequence = ++next_sequence;
    whisper::payload             request;
    request.write<uint8_t>(kProtocolVersion)
        .write<uint8_t>(kCmdStatus)
        .write<uint8_t>(part_id)
        .write<uint8_t>(region_id)
        .write<uint16_t>(sequence)
        .write<uint16_t>(0U)
        .write<uint16_t>(0U)
        .write<uint16_t>(0U);
    whisper_->get(
        kCalibrationChannel, request,
        [&](whisper::payload& response) {
            result = decode_calibration_response(response, kCmdStatus, part_id, sequence, 0U);
        },
        [&](const std::string& error) { result.error = QString::fromStdString(error); },
        std::chrono::milliseconds(1500));
    return result;
}

CalibrationRegionMaskResult Device::query_calibration_all()
{
    CalibrationRegionMaskResult result;
    if (!ready()) {
        result.error = QStringLiteral("设备未连接");
        return result;
    }
    static std::atomic<uint16_t> next_sequence{ 0U };
    const uint16_t               sequence = ++next_sequence;
    whisper::payload             request;
    // STATUS_ALL 不针对单个区域，Endpoint/Region 填 0。
    request.write<uint8_t>(kProtocolVersion)
        .write<uint8_t>(kCmdStatusAll)
        .write<uint8_t>(0U)
        .write<uint8_t>(0U)
        .write<uint16_t>(sequence)
        .write<uint16_t>(0U)
        .write<uint16_t>(0U)
        .write<uint16_t>(0U);
    whisper_->get(
        kCalibrationChannel, request,
        [&](whisper::payload& response) {
            if (response.size() < 24U) {
                result.error = QStringLiteral("区域状态响应长度不足");
                return;
            }
            const uint8_t version = response.read<uint8_t>();
            const uint8_t command = response.read<uint8_t>();
            response.read<uint8_t>(); // endpoint
            response.read<uint8_t>(); // region
            const uint16_t response_sequence = response.read<uint16_t>();
            response.read<uint16_t>(); // session
            const uint16_t data_len = response.read<uint16_t>();
            response.read<uint16_t>(); // reserved
            if (version != kProtocolVersion || command != (kCmdStatusAll | 0x80U) ||
                response_sequence != sequence || data_len < 12U) {
                result.error = QStringLiteral("区域状态响应头不匹配");
                return;
            }
            // [0] Status [1] RegionCount [2..3] CalibratedMask [4..5] ForceModeMask [6..11] 保留
            result.status          = response.read<uint8_t>();
            result.region_count    = response.read<uint8_t>();
            result.calibrated_mask = response.read<uint16_t>();
            result.force_mask      = response.read<uint16_t>();
            result.success         = result.status == 0U;
            if (!result.success)
                result.error = QStringLiteral("设备返回错误 0x%1")
                                   .arg(result.status, 2, 16, QLatin1Char('0'))
                                   .toUpper();
        },
        [&](const std::string& error) { result.error = QString::fromStdString(error); },
        std::chrono::milliseconds(1500));
    return result;
}

CalibrationDownloadResult Device::erase_calibration_lut(uint8_t part_id, uint8_t region_id)
{
    CalibrationDownloadResult result;
    if (!ready() || part_id < 1U || part_id > 6U || region_id > 3U) {
        result.error = QStringLiteral("设备未连接或部位/区域编号无效");
        return result;
    }
    static std::atomic<uint16_t> next_sequence{ 0U };
    const uint16_t               sequence   = ++next_sequence;
    const bool                   one_region = region_id != 0U;
    whisper::payload             request;
    // region_id 为 0：DataLength=0，擦整根手指（请求头区域填 1，与旧上位机一致）。
    // region_id 非 0：带 1 字节 Scope=0x01，只擦请求头里的这个区域。
    request.write<uint8_t>(kProtocolVersion)
        .write<uint8_t>(kCmdErase)
        .write<uint8_t>(part_id)
        .write<uint8_t>(one_region ? region_id : 1U)
        .write<uint16_t>(sequence)
        .write<uint16_t>(0U)
        .write<uint16_t>(one_region ? 1U : 0U)
        .write<uint16_t>(0U);
    if (one_region) request.write<uint8_t>(kEraseScopeRegion);
    whisper_->get(
        kCalibrationChannel, request,
        [&](whisper::payload& response) {
            result = decode_calibration_response(response, kCmdErase, part_id, sequence, 0U);
        },
        [&](const std::string& error) { result.error = QString::fromStdString(error); },
        std::chrono::milliseconds(2000));
    return result;
}

CalibrationDownloadResult Device::set_calibration_mode(uint8_t part_id, uint8_t mode)
{
    CalibrationDownloadResult result;
    if (!ready() || part_id < 1U || part_id > 6U) {
        result.error = QStringLiteral("设备未连接或部位编号无效");
        return result;
    }
    static std::atomic<uint16_t> next_sequence{ 0U };
    const uint16_t               sequence = ++next_sequence;
    const uint8_t                command  = mode == 0xffU ? kCmdStatus : kCmdSetMode;
    whisper::payload             request;
    request.write<uint8_t>(kProtocolVersion)
        .write<uint8_t>(command)
        .write<uint8_t>(part_id)
        .write<uint8_t>(1U)
        .write<uint16_t>(sequence)
        .write<uint16_t>(0U)
        .write<uint16_t>(mode == 0xffU ? 0U : 1U)
        .write<uint16_t>(0U);
    if (mode != 0xffU) request.write<uint8_t>(mode);
    whisper_->get(
        kCalibrationChannel, request,
        [&](whisper::payload& response) {
            result = decode_calibration_response(response, command, part_id, sequence, 0U);
        },
        [&](const std::string& error) { result.error = QString::fromStdString(error); },
        std::chrono::milliseconds(1500));
    return result;
}

CalibrationDownloadResult Device::download_calibration_lut(uint8_t part_id, const QVector<float>& adc,
                                                           const QVector<float>& force_cn, uint32_t version,
                                                           uint8_t region_id)
{
    CalibrationDownloadResult result;
    if (!ready() || part_id < 1U || part_id > 6U || adc.size() != force_cn.size() || adc.size() < 2 ||
        adc.size() > 62 || region_id < 1U || region_id > 6U) {
        result.error = QStringLiteral("设备未连接，或 LUT 点数/部位参数无效");
        return result;
    }
    QByteArray raw;
    raw.resize(adc.size() * 8);
    for (qsizetype i = 0; i < adc.size(); ++i) {
        if (!std::isfinite(adc[i]) || !std::isfinite(force_cn[i]) || adc[i] < 0.0f || force_cn[i] < 0.0f ||
            (i > 0 && (adc[i] <= adc[i - 1] || force_cn[i] < force_cn[i - 1]))) {
            result.error = QStringLiteral("LUT 必须有限、非负，且 AD 严格递增、力单调不降");
            return result;
        }
        std::memcpy(raw.data() + i * 8, &adc[i], 4U);
        std::memcpy(raw.data() + i * 8 + 4, &force_cn[i], 4U);
    }
    const uint32_t crc = calibration_crc32(reinterpret_cast<const uint8_t *>(raw.constData()), raw.size());
    uint16_t       session = static_cast<uint16_t>(QDateTime::currentMSecsSinceEpoch() & 0xffffU);
    if (session == 0U) session = 1U;
    static std::atomic<uint16_t> next_sequence{ 0U };

    auto request = [&](uint8_t command, const whisper::payload& data) {
        CalibrationDownloadResult response_result;
        const uint16_t            sequence = ++next_sequence;
        whisper::payload          packet;
        packet.write<uint8_t>(kProtocolVersion)
            .write<uint8_t>(command)
            .write<uint8_t>(part_id)
            .write<uint8_t>(region_id)
            .write<uint16_t>(sequence)
            .write<uint16_t>(session)
            .write<uint16_t>(static_cast<uint16_t>(data.size()))
            .write<uint16_t>(0U)
            .write(data.data(), data.size());
        whisper_->get(
            kCalibrationChannel, packet,
            [&](whisper::payload& response) {
                response_result =
                    decode_calibration_response(response, command, part_id, sequence, session);
            },
            [&](const std::string& error) { response_result.error = QString::fromStdString(error); },
            std::chrono::milliseconds(1500));
        return response_result;
    };

    whisper::payload begin;
    begin.write<uint8_t>(static_cast<uint8_t>(adc.size()))
        .write<uint8_t>(1U)
        .write<uint16_t>(static_cast<uint16_t>(raw.size()))
        .write<uint32_t>(crc)
        .write<uint32_t>(version)
        .write<uint32_t>(static_cast<uint32_t>(QDateTime::currentSecsSinceEpoch()))
        .write<uint8_t>(3U)
        .write<uint8_t>(0U)
        .write<uint8_t>(0U)
        .write<uint8_t>(0U);
    result = request(kCmdBegin, begin);
    if (!result.success) return result;

    for (int first = 0; first < adc.size(); first += 20) {
        const int        count = std::min<int>(20, static_cast<int>(adc.size()) - first);
        const QByteArray chunk = raw.mid(first * 8, count * 8);
        whisper::payload write;
        write.write<uint8_t>(static_cast<uint8_t>(first))
            .write<uint8_t>(static_cast<uint8_t>(count))
            .write<uint16_t>(static_cast<uint16_t>(chunk.size()))
            .write<uint32_t>(
                calibration_crc32(reinterpret_cast<const uint8_t *>(chunk.constData()), chunk.size()))
            .write(chunk.constData(), chunk.size());
        result = request(kCmdWrite, write);
        if (!result.success) {
            request(kCmdAbort, whisper::payload{});
            return result;
        }
    }
    result = request(kCmdVerify, whisper::payload{});
    if (!result.success) {
        request(kCmdAbort, whisper::payload{});
        return result;
    }
    whisper::payload apply;
    apply.write<uint8_t>(1U).write<uint8_t>(2U).write<uint16_t>(0U);
    result = request(kCmdApply, apply);
    if (!result.success) {
        request(kCmdAbort, whisper::payload{});
        return result;
    }
    whisper::payload commit;
    commit.write<uint32_t>(crc)
        .write<uint32_t>(version)
        .write<uint8_t>(1U)
        .write<uint8_t>(0U)
        .write<uint16_t>(0U);
    result = request(kCmdCommit, commit);
    if (result.success && result.crc32 != crc) {
        result.success = false;
        result.error   = QStringLiteral("提交响应 CRC32 与下发 LUT 不一致");
    }
    if (!result.success) return result;

    // COMMIT 的成功应答只能证明提交命令已执行。再读一次持久化状态，确保 MCU 保存的
    // 区域、点数、CRC 和版本都与本次发送一致，避免掉电或 Flash 写入异常被当成成功。
    // 强脑固件在 14 个区域全部提交后会复位一次，最后一个区域的这次核验可能赶上设备重启。
    // 只在完全没有应答（超时，status 仍为初值 0xff）时重试；设备返回了错误码则不重试。
    CalibrationDownloadResult persisted = query_calibration_status(part_id, region_id);
    for (int retry = 0; retry < 3 && !persisted.success && persisted.status == 0xffU; ++retry) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        persisted = query_calibration_status(part_id, region_id);
    }
    if (!persisted.success) {
        persisted.error = QStringLiteral("LUT 已提交，但 STATUS 持久化核验失败：%1").arg(persisted.error);
        return persisted;
    }
    // 章鱼旧固件在重启后的 STATUS 中可能不恢复 point_count，但会恢复持久化 CRC/version。
    // 新固件返回非零点数时仍严格核对，兼顾旧产品和强脑区域协议。
    const bool points_match = persisted.expected_points == 0U ||
                              persisted.expected_points == static_cast<uint8_t>(adc.size());
    if (!points_match || persisted.crc32 != crc || persisted.version != version) {
        persisted.success = false;
        persisted.error = QStringLiteral("STATUS 与下发 LUT 不一致：points=%1/%2, crc=0x%3/0x%4, version=%5/%6")
                              .arg(persisted.expected_points)
                              .arg(adc.size())
                              .arg(persisted.crc32, 8, 16, QLatin1Char('0'))
                              .arg(crc, 8, 16, QLatin1Char('0'))
                              .arg(persisted.version)
                              .arg(version)
                              .toUpper();
    }
    return persisted;
}
