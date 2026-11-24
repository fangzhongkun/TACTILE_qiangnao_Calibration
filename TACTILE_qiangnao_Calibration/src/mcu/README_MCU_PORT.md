# MCU 端移植说明

## 当前推荐算法

本目录下 `force_lut_calculator.c/.h` 实现的是 MCU 可移植版本的增强 area LUT 算法：

```text
raw[rows*cols]
-> 动态特征提取
-> 每个 area 1D sensor_sum-force 反查
-> 位置 + 有效接触面积 + 峰值差异的 KNN 加权融合
-> 输出 force
```

## 运行时特征

`force_lut_extract_feature_u16()` 会从任意尺寸矩阵中提取：

```text
sensor_sum = Σ raw_i
peak = max(raw_i)
active_count = count(raw_i > FORCE_LUT_ACTIVE_VALUE_THRESHOLD)
area_eff = sensor_sum² / Σ(raw_i²)
cx/cy = 归一化质心
var_x/var_y = 归一化坐标下的分布方差
```

这使同一套 MCU 代码可以兼容 6×4、8×10、12×8 等尺寸。

## 参数加载

推荐使用：

```c
force_lut_load_feature_table_for_part_ex()
```

它支持加载：

```text
area_cx[area]
area_cy[area]
force_grid[force]
sensor_table[area][force]
area_eff_table[area][force]
active_count_table[area][force]
peak_table[area][force]
```

如果只加载旧版 `sensor_table`，算法会自动退化为纯位置 KNN 加权。

## 预测接口

推荐直接传入原始矩阵：

```c
force_lut_predict_pressure_for_part_from_u16(part_id, raw, rows, cols)
```

调试时使用：

```c
force_lut_debug_predict_for_part_from_u16(..., ForceLutPredictDebugC *debug)
```

`debug` 中可以看到：

```text
输入特征
选中的 K 个 area
每个 area 的权重
每个 area 的预测 force
最终融合 force
```

## MCU 资源

默认容量：

```text
area <= 32
force_grid <= 64
KNN K = 4
```

每个 area 保存 4 条曲线：

```text
sensor / area_eff / active_count / peak
```

如果 MCU 内存紧张，可以：

1. 减小 `FORCE_LUT_MAX_AREA_NUM`；
2. 减小 `FORCE_LUT_MAX_FORCE_NUM`；
3. 只加载 `sensor_table` 和 `area_eff_table`，不加载 peak/active_count；
4. 把 `FORCE_LUT_K_NEIGHBOR` 改成 3。

## 注意事项

- 不同尺寸传感器仍需分别标定；
- PC 端和 MCU 端必须使用同样的特征定义；
- `A_eff` 是关键面积特征，用来补偿不同压力下触发点数量变化带来的误差；
- 当前版本不需要 `force_model_coeff.h`，参数仍通过模拟 MCU 写入 C/H 运行时表。
