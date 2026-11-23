#ifndef MULTI_PART_CALIBRATION_SERVICE_H
#define MULTI_PART_CALIBRATION_SERVICE_H

#include <map>
#include <memory>
#include <QString>
#include <QStringList>
#include <vector>

namespace app
{

    // 手部部位定义
    enum class HandPart : uint8_t
    {
        THUMB  = 0x01, // 大拇指
        INDEX  = 0x02, // 食指
        MIDDLE = 0x03, // 中指
        RING   = 0x04, // 无名指
        PINKY  = 0x05, // 小指
    };

    // 部位标定状态
    struct PartCalibrationStatus
    {
        HandPart part;
        bool     lut_loaded       = false;
        bool     zeroing_enabled  = false;
        bool     zeroing_finished = false;
        int      area_count       = 0;
        int      force_count      = 0;
        int      sample_count     = 0;
        QString  last_error;
    };

    // 多部位标定结果
    struct MultiPartCalibrationResult
    {
        bool                                      success = false;
        QString                                   error_message;
        std::map<HandPart, PartCalibrationStatus> parts_status;
        int                                       total_parts     = 0;
        int                                       succeeded_parts = 0;
        int                                       failed_parts    = 0;
    };

    class MultiPartCalibrationService
    {
    public:
        /**
         * @brief 为指定部位加载标定数据
         * @param part 部位ID
         * @param csv_paths 该部位的标定CSV文件列表
         * @return 标定结果
         */
        static MultiPartCalibrationResult calibratePartFromCsv(HandPart part, const QStringList& csv_paths);

        /**
         * @brief 批量标定所有部位
         * @param parts_csv_map 部位到CSV文件列表的映射
         * @return 综合标定结果
         */
        static MultiPartCalibrationResult
        calibrateAllParts(const std::map<HandPart, QStringList>& parts_csv_map);

        /**
         * @brief 查询指定部位的标定状态
         * @param part 部位ID
         * @return 标定状态
         */
        static PartCalibrationStatus queryPartStatus(HandPart part);

        /**
         * @brief 清除指定部位的标定数据
         * @param part 部位ID
         * @return 是否成功
         */
        static bool clearPartCalibration(HandPart part);

        /**
         * @brief 清除所有部位的标定数据
         */
        static void clearAllCalibrations();

        /**
         * @brief 部位枚举到协议ID的转换
         */
        static uint8_t partToProtocolId(HandPart part);

        /**
         * @brief 协议ID到部位枚举的转换
         */
        static HandPart protocolIdToPart(uint8_t protocol_id);

        /**
         * @brief 获取部位的中文名称
         */
        static QString getPartName(HandPart part);

        /**
         * @brief 获取所有支持的部位列表
         */
        static std::vector<HandPart> getAllParts();
    };

} // namespace app

#endif // MULTI_PART_CALIBRATION_SERVICE_H
