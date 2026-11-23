#ifndef SCANNERPT_DOMAIN_CALIBRATION_RECORD_H
#define SCANNERPT_DOMAIN_CALIBRATION_RECORD_H

namespace scannerpt::domain {

// 单次标定采集记录：
// diff_ad 字段名保留兼容历史代码；当前语义为矩阵总值 matrix_sum。
// 保留该结构体用于兼容旧工程文件中仍被编译的 calibration_service / calibration_session 等遗留模块。
struct PressureMeasurement
{
    double diff_ad = 0.0;
    double force   = 0.0;
    double cx      = 0.0;
    double cy      = 0.0;
};

// 单次精度测试记录：用于测试结果保存和报告生成。
struct TestRecord
{
    double input_pressure = 0.0;
    // 测试 CSV 中的“传感器”：当前整张传感器矩阵的合。
    double sensor_value   = 0.0;
    // 误差(g) = 传感器矩阵合 - 输入压力。
    double diff           = 0.0;
    // 误差百分比(%) = 误差(g) / 输入压力 * 100。
    double ratio_percent  = 0.0;
    // 当前测试所属区域，用于在同一个 CSV 中展开 cx_N / cy_N 列。
    int    area_index     = 1;
    double cx             = 0.0;
    double cy             = 0.0;
};

} // namespace scannerpt::domain

#endif // SCANNERPT_DOMAIN_CALIBRATION_RECORD_H
