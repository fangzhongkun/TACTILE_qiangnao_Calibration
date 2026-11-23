# LUT 串口下发改动说明

本版本基于现有 `Device::calibration_curve(float*, count, package_id)` 通信协议做最小扩展，优先只改上位机逻辑。

## 下发包约定

仍走原通道与原帧格式：

```text
cmd = (package_id << 4) | 0x03
count = uint8_t float_count
payload = float[count]
```

新增：

```text
package_id = 0：meta = [part_id, rows, cols, area_num, force_num, has_shape_feature]
package_id = 1：area_cx[area]
package_id = 2：area_cy[area]
package_id = 3：force_grid[force]
package_id = 4：sensor_table[area][force]
```

每个部位按 `0 -> 1 -> 2 -> 3 -> 4` 顺序下发，`package_id=4` 作为该部位基础 LUT 收齐标志。

## 多部位下发顺序

点击“标定传感器”后，上位机会按以下部位循环下发：

```text
0x11, 0x21, 0x31, 0x41, 0x51, 0x61
```

每个部位分别查找该部位的 CSV，建表成功才下发；没有 CSV 的部位会跳过并提示。

## 容量约束

当前协议 `count` 是 `uint8_t`，单包最多 255 个 float。为避免分包，已将：

```c
FORCE_LUT_MAX_AREA_NUM  = 20
FORCE_LUT_MAX_FORCE_NUM = 62
```

所以 `sensor_table` 最大为 `20 * 12 = 240` 个 float，可以继续单包下发。

如果以后恢复 `32 * 64`，必须把协议改成分包，不能继续使用当前单包 count。

## 修改文件

```text
application/calibration_test_service.h
application/calibration_test_service.cpp
force-calibration/sensor_.h
force-calibration/sensor_.cpp
devices/device.cpp
mcu/force_lut_calculator.h
```
