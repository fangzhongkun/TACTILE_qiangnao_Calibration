#include "multi_part_calibration_service.h"

#include "calibration_test_service.h"
#include "logging.h"

#include <algorithm>

namespace app
{

    uint8_t MultiPartCalibrationService::partToProtocolId(HandPart part)
    {
        // 将部位映射到协议ID（与固件约定一致）
        return static_cast<uint8_t>(part) << 4 | 0x01;
    }

    HandPart MultiPartCalibrationService::protocolIdToPart(uint8_t protocol_id)
    {
        uint8_t part_id = (protocol_id >> 4) & 0x0F;
        return static_cast<HandPart>(part_id);
    }

    QString MultiPartCalibrationService::getPartName(HandPart part)
    {
        switch (part) {
        case HandPart::THUMB:  return QStringLiteral("大拇指");
        case HandPart::INDEX:  return QStringLiteral("食指");
        case HandPart::MIDDLE: return QStringLiteral("中指");
        case HandPart::RING:   return QStringLiteral("无名指");
        case HandPart::PINKY:  return QStringLiteral("小指");
        default:               return QStringLiteral("未知部位");
        }
    }

    std::vector<HandPart> MultiPartCalibrationService::getAllParts()
    {
        return { HandPart::THUMB, HandPart::INDEX, HandPart::MIDDLE, HandPart::RING, HandPart::PINKY };
    }

    MultiPartCalibrationResult
    MultiPartCalibrationService::calibratePartFromCsv(HandPart part, const QStringList& csv_paths)
    {
        MultiPartCalibrationResult result;
        result.total_parts = 1;

        if (csv_paths.isEmpty()) {
            result.error_message = QStringLiteral("CSV文件列表为空");
            loge("Calibrate part {} failed: empty CSV list", static_cast<int>(part));
            return result;
        }

        const uint8_t protocol_id = partToProtocolId(part);
        const QString part_name   = getPartName(part);

        logi("开始标定部位: {} (protocol_id=0x{:02X})", part_name.toStdString(), protocol_id);

        // 调用现有的标定服务
        auto cal_result = CalibrationTestService::simulateCalibrationToMcuForPart(protocol_id, csv_paths);

        PartCalibrationStatus status;
        status.part         = part;
        status.lut_loaded   = cal_result.success;
        status.area_count   = cal_result.area_num;
        status.force_count  = cal_result.force_num;
        status.sample_count = cal_result.sample_count;

        if (cal_result.success) {
            result.success         = true;
            result.succeeded_parts = 1;
            logi("部位 {} 标定成功: area={}, force={}, samples={}", part_name.toStdString(),
                 status.area_count, status.force_count, status.sample_count);
        }
        else {
            result.failed_parts  = 1;
            result.error_message = cal_result.error_message;
            status.last_error    = cal_result.error_message;
            loge("部位 {} 标定失败: {}", part_name.toStdString(), cal_result.error_message.toStdString());
        }

        result.parts_status[part] = status;
        return result;
    }

    MultiPartCalibrationResult
    MultiPartCalibrationService::calibrateAllParts(const std::map<HandPart, QStringList>& parts_csv_map)
    {
        MultiPartCalibrationResult result;
        result.total_parts     = static_cast<int>(parts_csv_map.size());
        result.succeeded_parts = 0;
        result.failed_parts    = 0;

        if (parts_csv_map.empty()) {
            result.error_message = QStringLiteral("没有提供任何部位的标定数据");
            loge("calibrateAllParts failed: empty parts map");
            return result;
        }

        logi("========================================");
        logi("开始批量标定 {} 个部位", result.total_parts);
        logi("========================================");

        // 按部位顺序逐个标定
        std::vector<HandPart> parts_order = getAllParts();

        for (const auto& part : parts_order) {
            auto it = parts_csv_map.find(part);
            if (it == parts_csv_map.end()) {
                continue; // 该部位没有提供标定数据，跳过
            }

            const QStringList& csv_paths = it->second;
            if (csv_paths.isEmpty()) {
                logi("跳过部位 {}: 没有CSV文件", getPartName(part).toStdString());
                continue;
            }

            // 标定单个部位
            auto part_result = calibratePartFromCsv(part, csv_paths);

            if (part_result.success) {
                result.succeeded_parts++;
            }
            else {
                result.failed_parts++;
            }

            // 合并状态
            result.parts_status.insert(part_result.parts_status.begin(), part_result.parts_status.end());
        }

        result.success = (result.failed_parts == 0 && result.succeeded_parts > 0);

        if (result.success) {
            logi("========================================");
            logi("批量标定完成: 成功 {}/{}", result.succeeded_parts, result.total_parts);
            logi("========================================");
        }
        else {
            loge("========================================");
            loge("批量标定失败: 成功 {}, 失败 {}, 总计 {}", result.succeeded_parts, result.failed_parts,
                 result.total_parts);
            loge("========================================");

            if (result.succeeded_parts == 0) {
                result.error_message = QStringLiteral("所有部位标定均失败");
            }
            else {
                result.error_message = QStringLiteral("部分部位标定失败");
            }
        }

        return result;
    }

    PartCalibrationStatus MultiPartCalibrationService::queryPartStatus(HandPart part)
    {
        PartCalibrationStatus status;
        status.part = part;

        const uint8_t protocol_id = partToProtocolId(part);
        status.lut_loaded         = CalibrationTestService::hasLoadedLutForPart(protocol_id);

        if (!status.lut_loaded) {
            status.last_error = QStringLiteral("未加载标定数据");
        }

        return status;
    }

    bool MultiPartCalibrationService::clearPartCalibration(HandPart part)
    {
        const uint8_t protocol_id = partToProtocolId(part);
        const QString part_name   = getPartName(part);

        logi("清除部位 {} (0x{:02X}) 的标定数据", part_name.toStdString(), protocol_id);

        // 注意：当前实现的 clearLoadedLut() 是全局清除
        // 如需单独清除某个部位，需要扩展底层接口
        // 这里先返回true，实际清除由底层MCU管理

        return true;
    }

    void MultiPartCalibrationService::clearAllCalibrations()
    {
        logi("清除所有部位的标定数据");
        CalibrationTestService::clearLoadedLut();
    }

} // namespace app
