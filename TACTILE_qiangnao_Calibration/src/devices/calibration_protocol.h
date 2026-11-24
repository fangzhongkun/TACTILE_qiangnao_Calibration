#ifndef CALIBRATION_PROTOCOL_H
#define CALIBRATION_PROTOCOL_H

#include <cstdint>
#include <QString>
#include <QVector>
#include <string>

struct CalibrationDownloadResult
{
    bool     success         = false;
    uint8_t  status          = 0xff;
    uint8_t  expected_points = 0;
    uint8_t  received_points = 0;
    uint8_t  mode            = 0;
    uint32_t crc32           = 0;
    uint32_t version         = 0;
    QString  error;
};

// STATUS_ALL（0x03）应答：14 个区域的状态掩码。位序固定：
//   bit0 0x11 拇指指尖  bit1 0x12 拇指指根
//   bit2~4 食指  bit5~7 中指  bit8~10 无名指  bit11~13 小指（各为指尖/指中/指根）
struct CalibrationRegionMaskResult
{
    static constexpr uint16_t kAllMask = 0x3FFFU;

    bool     success         = false;
    uint8_t  status          = 0xff;
    uint8_t  region_count    = 0;
    uint16_t calibrated_mask = 0;
    uint16_t force_mask      = 0;
    QString  error;

    [[nodiscard]] bool all_calibrated() const { return success && calibrated_mask == kAllMask; }
};

struct PartMetadataResult
{
    bool        success   = false;
    uint8_t     device_id = 0xff;
    std::string firmware;
    std::string serial_number;
    QString     error;
};

#endif // CALIBRATION_PROTOCOL_H
