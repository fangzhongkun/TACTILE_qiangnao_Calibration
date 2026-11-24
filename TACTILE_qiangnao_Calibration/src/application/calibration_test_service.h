#ifndef CALIBRATION_TEST_SERVICE_H
#define CALIBRATION_TEST_SERVICE_H

#include "calibration_capture_service.h"

#include <QString>
#include <QStringList>
#include <vector>

namespace app
{

    struct CalibrationProgramResult
    {
        bool    success = false;
        QString error_message;
        int     cx_num            = 0; // 离散 area 方案下表示 area_count
        int     cy_num            = 0; // 保留兼容字段，固定为 1
        int     force_num         = 0;
        int     sample_count      = 0;
        int     point_group_count = 0;

        // 串口下发 meta 包需要的运行时元信息。
        int  part_protocol_id  = 0;
        int  sensor_rows       = 0;
        int  sensor_cols       = 0;
        int  area_num          = 0;
        bool has_shape_feature = false;

        // 模拟标定生成并准备下发给 MCU 的数组。
        // 增强 area 方案：cx_grid/cy_grid 为 area 中心，force_grid 为压力轴，diff_ad_table 为
        // sensor_sum[area][force] 展平数组。 接触面积/峰值等扩展表由 CalibrationTestService 内部直接写入
        // MCU C/H 运行时表，旧字段保留用于 UI/日志兼容。
        std::vector<float> cx_grid;
        std::vector<float> cy_grid;
        std::vector<float> force_grid;
        std::vector<float> diff_ad_table;

        // 固件 V1 事务协议使用的一维部位 LUT。AD 为有效点平均值，力为 0.01N。
        std::vector<float> wire_adc;
        std::vector<float> wire_force_cn;
    };

    struct CalibrationTestResult
    {
        bool    success = false;
        QString error_message;
        double  predicted_force = 0.0;
        double  diff_ad         = 0.0;
        double  cx              = 0.0;
        double  cy              = 0.0;
        int     rows            = 0;
        int     cols            = 0;
    };

    class CalibrationTestService
    {
    public:
        /*
         * 架构职责说明：
         *   CalibrationTestService 属于上位机应用层。它负责读取 CSV、生成 LUT 表格，
         *   然后模拟“下发 MCU”的过程，把表格数组写入 force_lut_calculator。
         *
         *   force_lut_calculator 属于 MCU 运行时层。它不读取 CSV、不建表，
         *   只保存已经写入的表格，并根据实时矩阵特征执行 KNN 加权 area LUT 预测。
         */
        // 模拟“标定下发”：PC 读取 CSV 并生成表格，然后通过数组接口写入 MCU 计算模块。
        static CalibrationProgramResult simulateCalibrationToMcu(const QString& csv_path);
        static CalibrationProgramResult simulateCalibrationToMcu(const QStringList& csv_paths);
        static CalibrationProgramResult simulateCalibrationToMcuForPart(int                part_protocol_id,
                                                                        const QStringList& csv_paths);

        // 模拟“运行时测试”：不再读取 CSV、不再重新建表。
        // 旧入口把实时 CaptureSample 特征传给 MCU；新入口直接把 raw[rows*cols] 传给 MCU 计算模块。
        static CalibrationTestResult runOnce(const CaptureSample& realtime_sample);
        static CalibrationTestResult runOnceForPart(int                  part_protocol_id,
                                                    const CaptureSample& realtime_sample);

        // 推荐运行时入口：直接把当前阵列原始数据交给 MCU C/H 层，
        // 由 force_lut_calculator.c 根据 rows/cols 计算 sensor_sum、cx/cy、peak、active_count、A_eff
        // 等特征， 从而兼容 6x4、8x10、12x8 等不同尺寸。
        static CalibrationTestResult
        runOnceForPartFromMatrix(int part_protocol_id, const touch::matrix<uint16_t>& realtime_matrix);

        static bool hasLoadedLut();
        static bool hasLoadedLutForPart(int part_protocol_id);
        static void clearLoadedLut();
    };

} // namespace app

#endif // CALIBRATION_TEST_SERVICE_H
