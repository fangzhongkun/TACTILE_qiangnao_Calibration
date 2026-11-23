# 测试记录改动说明：已标定时使用真实上传值

## 改动目标

原测试流程固定通过上位机内置的 `force_lut_calculator.c/.h` 模拟 MCU 端预测，并把模拟预测力写入测试 CSV 的“传感器”列。

本次改为：

- 如果识别到设备已经标定，则直接使用真实传感器实时上传矩阵本身的值写入测试记录；
- 如果未识别到设备已标定，则保留原模拟 MCU 预测逻辑作为兼容 fallback。

## 修改位置

主要修改文件：

```text
force-calibration/sensor_.cpp
```

核心函数：

```cpp
SensorWidget::run_calibration_test_once()
```

## 新测试逻辑

测试按钮点击后流程如下：

```text
读取当前部位实时矩阵
  ↓
生成 realtime_sample
  ↓
判断设备标定状态
  ↓
如果 calibration_state_value_ != 0：
    使用真实上传矩阵总值 realtime_sample.diff_ad 作为“传感器”列
否则：
    使用 CalibrationTestService::runOnceForPartFromMatrix() 模拟 MCU 预测
  ↓
计算误差、误差百分比、满量程误差
  ↓
写入 test_<SN>_id_xx.csv
```

## 真实上传值定义

当前设备上传的是矩阵，因此测试 CSV 中“传感器”列使用：

```cpp
sensor_value = realtime_sample.diff_ad;
```

其中 `realtime_sample.diff_ad` 在当前工程中的语义已经改为：

```text
当前部位实时矩阵总值 matrix_sum
```

即：

```cpp
matrix_sum = sum(raw_matrix[y * cols + x])
```

## 已标定状态识别

优先使用界面中已经收到的标定状态：

```cpp
calibration_query_received_ && calibration_state_value_ != 0
```

如果当前还没有识别到已标定状态，测试时会主动查询一次：

```cpp
query_calibration_state_from_device(500)
```

若查询成功且返回非 0，则使用真实上传值；否则回退到模拟 MCU 预测。

## 保留兼容逻辑

未标定或查询失败时，仍然使用旧逻辑：

```cpp
CalibrationTestService::runOnceForPartFromMatrix(collect_part_protocol_id(), m)
```

这样不会影响当前没有真实 MCU 标定功能的调试流程。

## CSV 格式

CSV 表头保持不变：

```text
压力,传感器,误差(g),误差百分比(%),误差判断,满量程误差百分比(%),满量程误差判断,cx,cy
```

其中“传感器”列含义变为：

```text
已标定：真实上传矩阵总值
未标定：模拟 MCU 预测力
```

日志与 Toast 中会显示当前来源：

```text
真实上传值
模拟MCU预测
```
