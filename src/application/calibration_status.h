#ifndef CALIBRATION_STATUS_H
#define CALIBRATION_STATUS_H

#include <QString>
#include <cstdint>

namespace app {

enum class CalibrationStatus
{
    Unknown,
    Querying,
    Calibrated,
    NotCalibrated
};

inline CalibrationStatus calibrationStatusFromDeviceReply(uint8_t value)
{
    return value != 0 ? CalibrationStatus::Calibrated : CalibrationStatus::NotCalibrated;
}

inline QString calibrationStatusText(CalibrationStatus status)
{
    switch (status) {
    case CalibrationStatus::Unknown:       return QStringLiteral("未查询");
    case CalibrationStatus::Querying:      return QStringLiteral("查询中...");
    case CalibrationStatus::Calibrated:    return QStringLiteral("已标定");
    case CalibrationStatus::NotCalibrated: return QStringLiteral("未标定");
    }
    return QStringLiteral("未查询");
}

inline const char *calibrationStatusLogText(CalibrationStatus status)
{
    switch (status) {
    case CalibrationStatus::Unknown:       return "未查询";
    case CalibrationStatus::Querying:      return "查询中";
    case CalibrationStatus::Calibrated:    return "已标定";
    case CalibrationStatus::NotCalibrated: return "未标定";
    }
    return "未查询";
}

} // namespace app

#endif // CALIBRATION_STATUS_H
