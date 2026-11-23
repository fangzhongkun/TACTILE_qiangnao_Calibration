# 编译/检查报告

## 检查范围

当前压缩包仍然只有 `src/` 目录，缺少完整 CMake/qmake 工程文件，并且当前执行环境没有 Qt、libtouch、whisper、probe、glog、fmt 等完整依赖，因此无法在此环境完成整个 Qt 上位机的真实链接构建。

本次已完成以下可检查项：

1. 检查所有本地 quote include，未发现缺失的本地头文件。
2. 检查 `force-calibration/sensor_.cpp` 花括号平衡，结果正常。
3. 使用 gcc 编译检查 `mcu/force_lut_calculator.c`，通过。
4. 使用 g++ 编译检查 `csv/csv.cpp`，通过。
5. 使用 g++ 编译检查 legacy stub：
   - `application/calibration_service.cpp`，通过。
   - `force-calibration/mult-lut.cpp`，通过。

## 本次主要修改

修改 `SensorWidget::run_calibration_test_once()`：

- 已标定状态：测试记录使用真实传感器实时上传矩阵本身值，即当前部位矩阵总值。
- 未标定或查询失败：保留原上位机 C/H 模拟 MCU 预测逻辑。

