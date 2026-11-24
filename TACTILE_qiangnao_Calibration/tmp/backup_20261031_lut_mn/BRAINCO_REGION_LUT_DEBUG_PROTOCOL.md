# 强脑手套分区域 LUT 串口协议

## 串口参数

```text
COM4，2000000 baud，8N1，无流控，小端
```

所有帧按十六进制发送。代码块中的空格和换行只用于阅读。

## 区域编号

| Endpoint | 部位 | Region |
|---:|---|---:|
| `0x01` | 拇指 | `0x01`～`0x02` |
| `0x02` | 食指 | `0x01`～`0x03` |
| `0x03` | 中指 | `0x01`～`0x03` |
| `0x04` | 无名指 | `0x01`～`0x03` |
| `0x05` | 小指 | `0x01`～`0x03` |

界面编号 `0x31` 在线上传输为 `Endpoint=0x03、Region=0x01`。每个区域独立下发。

拇指的 Region 1 为指尖、Region 2 为指根；其余手指 Region 1/2/3 依次为指尖/指中/指根。全手共 14 个区域。

### 区域状态掩码位序

STATUS_ALL 应答用 16 位掩码表示 14 个区域，位序固定：

| 位 | 区域 | 位 | 区域 | 位 | 区域 |
|---:|---|---:|---|---:|---|
| bit0 | `0x11` 拇指指尖 | bit5 | `0x31` 中指指尖 | bit10 | `0x43` 无名指指根 |
| bit1 | `0x12` 拇指指根 | bit6 | `0x32` 中指指中 | bit11 | `0x51` 小指指尖 |
| bit2 | `0x21` 食指指尖 | bit7 | `0x33` 中指指根 | bit12 | `0x52` 小指指中 |
| bit3 | `0x22` 食指指中 | bit8 | `0x41` 无名指指尖 | bit13 | `0x53` 小指指根 |
| bit4 | `0x23` 食指指根 | bit9 | `0x42` 无名指指中 | bit14～15 | 保留，恒为 0 |

14 个区域全部满足时掩码为 `0x3FFF`。

## 外层帧

```text
3C 3C | Channel | Flags | PayloadLength:u16 | Payload | CRC16:u16 | 3E 3E
```

- Channel 固定为 `0x07`。
- 请求 Flags=`(PackageId<<2)|0x01`。
- 应答 Flags=`(PackageId<<2)|0x02`。
- PayloadLength、CRC16 和其他多字节字段均为小端。
- CRC16 只计算 Payload。

## Payload 固定头

```text
Protocol:u8 | Command:u8 | Endpoint:u8 | Region:u8 |
Sequence:u16 | Session:u16 | DataLength:u16 | Reserved:u16
```

Payload 固定头为 12 字节。Protocol 固定 `0x01`；Reserved 固定 0。应答必须回显 Endpoint、Region、Sequence、Session。应答 Command=`请求 Command|0x80`。

## 命令及作用

| 命令 | 请求 | 应答 | 作用 |
|---|---:|---:|---|
| STATUS | `0x02` | `0x82` | 查询区域标定状态 |
| STATUS_ALL | `0x03` | `0x83` | 一次查询 14 个区域的标定状态 |
| BEGIN | `0x10` | `0x90` | 建立 LUT 接收事务 |
| WRITE | `0x11` | `0x91` | 发送 LUT 点数组 |
| VERIFY | `0x12` | `0x92` | 校验完整性和单调性 |
| APPLY | `0x13` | `0x93` | 允许提交暂存数据 |
| COMMIT | `0x14` | `0x94` | 写入 Flash |
| ABORT | `0x16` | `0x96` | 取消未完成事务 |
| ERASE | `0x17` | `0x97` | 清除部位或单个区域的标定数据 |
| SET_MODE | `0x18` | `0x98` | 切换原始 AD/力值模式 |

正常下发顺序：

```text
STATUS → BEGIN → WRITE×N → VERIFY → APPLY → COMMIT → STATUS
```

每一帧收到 `Status=0x00` 后再发送下一帧。超时使用完全相同的请求帧重发，最多 3 次。

## 应答数据

所有命令的应答数据固定为 12 字节：

```text
Status:u8 | PointCount:u8 | ReceivedCount:u8 | Mode:u8 |
TableCRC32:u32 | DataId:u32
```

- Command 表示应答属于哪条命令，例如 `0x82` 是 STATUS 应答。
- Status 表示执行结果，`0x00` 为成功。
- `Status=0x00、PointCount=0` 表示 STATUS 查询成功，但该区域没有 LUT。
- Mode：`0x00` 原始 AD，`0x02` 力值。
- 例外：STATUS_ALL 的应答同样是 12 字节，但字段含义不同，见"STATUS_ALL"一节。

## 标定标志与复位

- 每个区域的"已标定"标志就是该区域的 LUT 是否已经 COMMIT 写入 Flash（Enabled=1）。标志随 LUT 一起保存在 Flash，掉电不丢；ERASE 会清除它。
- 全局"是否标定"（属性通道 `0x01` 命令 `0x02`，返回 1 字节）在 14 个区域全部已标定时为 `0x01`，否则为 `0x00`。它同时是力值输出的总开关：未全部标定时整只手一律上报原始 AD。
- **某次 COMMIT 让 14 个区域全部标定完成后，设备在发出这条 COMMIT 的应答后立即复位一次**，开机时从 Flash 重新读出各区域的标志。以后只重标某一个区域、再次凑满 14 个时同样会复位。
- 上位机对最后一个区域做"下发后核验"的 STATUS 时，设备可能正在重启。设备约 0.5 s 内恢复应答，上位机应按超时重发（每 300 ms 一次，总时长不短于 1.5 s），完全无应答时可再等待后重试。
- ⚠️ 复位后设备会自动重新采集零点（连续 64 帧）。**执行标定下发前必须移走手指上的压杆和砝码**，否则这个压力会被当成零点。

## 示例数据

```text
区域：0x31（中指区域 1）
AD：100、200、300、400、500、600、700、800、900、1000
Force：1、2、3、4、5、6、7、8、9、10 g
Session：0x3456
DataId：0x20260911
PointCount：10
TableBytes：80
TableCRC32：0x75C45DCF
```

一个 LUT 点为 `AD:float32 + Force:float32`，共 8 字节，IEEE 754 单精度、小端。AD 严格递增，Force 单调不减。

## 完整 LUT 下发问答

### STATUS（下发前查询）

作用：查询 0x31 当前是否有有效 LUT。本例返回未标定。

请求帧，共 22 字节：

```text
3C 3C 07 01 0C 00 01 02 03 01 01 00 00 00 00 00
00 00 CA 28 3E 3E
```

| 整帧偏移 | 十六进制字节 | 字段 | 含义 |
|---:|---|---|---|
| 0～1 | `3C 3C` | Header | 固定帧头 |
| 2 | `07` | Channel | `0x07`，标定通道 |
| 3 | `01` | Flags | PackageId=0,请求帧 |
| 4～5 | `0C 00` | PayloadLength | 12 字节，小端 |
| 6 | `01` | Protocol | 固定 `0x01` |
| 7 | `02` | Command | `0x02` |
| 8 | `03` | Endpoint | `0x03` |
| 9 | `01` | Region | `0x01` |
| 10～11 | `01 00` | Sequence | `0x0001`，小端 |
| 12～13 | `00 00` | Session | `0x0000`，小端 |
| 14～15 | `00 00` | DataLength | 0 字节，小端 |
| 16～17 | `00 00` | Reserved | 固定 0 |
| 18～19 | `CA 28` | CRC16 | `0x28CA`，只计算 Payload |
| 20～21 | `3E 3E` | Tail | 固定帧尾 |

应答帧，共 34 字节：

```text
3C 3C 07 02 18 00 01 82 03 01 01 00 00 00 0C 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 F7 9A
3E 3E
```

| 整帧偏移 | 十六进制字节 | 字段 | 含义 |
|---:|---|---|---|
| 0～1 | `3C 3C` | Header | 固定帧头 |
| 2 | `07` | Channel | `0x07`，标定通道 |
| 3 | `02` | Flags | PackageId=0,应答帧 |
| 4～5 | `18 00` | PayloadLength | 24 字节，小端 |
| 6 | `01` | Protocol | 固定 `0x01` |
| 7 | `82` | Command | `0x82` |
| 8 | `03` | Endpoint | `0x03` |
| 9 | `01` | Region | `0x01` |
| 10～11 | `01 00` | Sequence | `0x0001`，小端 |
| 12～13 | `00 00` | Session | `0x0000`，小端 |
| 14～15 | `0C 00` | DataLength | 12 字节，小端 |
| 16～17 | `00 00` | Reserved | 固定 0 |
| 18 | `00` | Status | `0x00`，成功 |
| 19 | `00` | PointCount | 0 |
| 20 | `00` | ReceivedCount | 0 |
| 21 | `00` | Mode | `0x00` 原始 AD |
| 22～25 | `00 00 00 00` | TableCRC32 | `0x00000000`，小端 |
| 26～29 | `00 00 00 00` | DataId | `0x00000000`，小端 |
| 30～31 | `F7 9A` | CRC16 | `0x9AF7`，只计算 Payload |
| 32～33 | `3E 3E` | Tail | 固定帧尾 |
### BEGIN

作用：建立接收事务，声明点数、字节数、整表 CRC32 和 DataId。

请求帧，共 42 字节：

```text
3C 3C 07 05 20 00 01 10 03 01 02 00 56 34 14 00
00 00 0A 01 50 00 CF 5D C4 75 11 09 26 20 00 00
00 00 03 00 00 00 24 B5 3E 3E
```

| 整帧偏移 | 十六进制字节 | 字段 | 含义 |
|---:|---|---|---|
| 0～1 | `3C 3C` | Header | 固定帧头 |
| 2 | `07` | Channel | `0x07`，标定通道 |
| 3 | `05` | Flags | PackageId=1,请求帧 |
| 4～5 | `20 00` | PayloadLength | 32 字节，小端 |
| 6 | `01` | Protocol | 固定 `0x01` |
| 7 | `10` | Command | `0x10` |
| 8 | `03` | Endpoint | `0x03` |
| 9 | `01` | Region | `0x01` |
| 10～11 | `02 00` | Sequence | `0x0002`，小端 |
| 12～13 | `56 34` | Session | `0x3456`，小端 |
| 14～15 | `14 00` | DataLength | 20 字节，小端 |
| 16～17 | `00 00` | Reserved | 固定 0 |
| 18 | `0A` | PointCount | 10 |
| 19 | `01` | DataFormat | `0x01`，AD/Force 交错 float32 |
| 20～21 | `50 00` | TableBytes | 80 字节 |
| 22～25 | `CF 5D C4 75` | TableCRC32 | `0x75C45DCF` |
| 26～29 | `11 09 26 20` | DataId | `0x20260911` |
| 30～33 | `00 00 00 00` | CaptureTime | 0 |
| 34 | `03` | Flags | `0x03` |
| 35～37 | `00 00 00` | Reserved | 固定 0 |
| 38～39 | `24 B5` | CRC16 | `0xB524`，只计算 Payload |
| 40～41 | `3E 3E` | Tail | 固定帧尾 |

应答帧，共 34 字节：

```text
3C 3C 07 06 18 00 01 90 03 01 02 00 56 34 0C 00
00 00 00 0A 00 00 CF 5D C4 75 11 09 26 20 F2 4E
3E 3E
```

| 整帧偏移 | 十六进制字节 | 字段 | 含义 |
|---:|---|---|---|
| 0～1 | `3C 3C` | Header | 固定帧头 |
| 2 | `07` | Channel | `0x07`，标定通道 |
| 3 | `06` | Flags | PackageId=1,应答帧 |
| 4～5 | `18 00` | PayloadLength | 24 字节，小端 |
| 6 | `01` | Protocol | 固定 `0x01` |
| 7 | `90` | Command | `0x90` |
| 8 | `03` | Endpoint | `0x03` |
| 9 | `01` | Region | `0x01` |
| 10～11 | `02 00` | Sequence | `0x0002`，小端 |
| 12～13 | `56 34` | Session | `0x3456`，小端 |
| 14～15 | `0C 00` | DataLength | 12 字节，小端 |
| 16～17 | `00 00` | Reserved | 固定 0 |
| 18 | `00` | Status | `0x00`，成功 |
| 19 | `0A` | PointCount | 10 |
| 20 | `00` | ReceivedCount | 0 |
| 21 | `00` | Mode | `0x00` 原始 AD |
| 22～25 | `CF 5D C4 75` | TableCRC32 | `0x75C45DCF`，小端 |
| 26～29 | `11 09 26 20` | DataId | `0x20260911`，小端 |
| 30～31 | `F2 4E` | CRC16 | `0x4EF2`，只计算 Payload |
| 32～33 | `3E 3E` | Tail | 固定帧尾 |
### WRITE

作用：发送 AD、Force 数据。本例 10 点，一帧发送完成。

请求帧，共 110 字节：

```text
3C 3C 07 09 64 00 01 11 03 01 03 00 56 34 58 00
00 00 00 0A 50 00 CF 5D C4 75 00 00 C8 42 00 00
80 3F 00 00 48 43 00 00 00 40 00 00 96 43 00 00
40 40 00 00 C8 43 00 00 80 40 00 00 FA 43 00 00
A0 40 00 00 16 44 00 00 C0 40 00 00 2F 44 00 00
E0 40 00 00 48 44 00 00 00 41 00 00 61 44 00 00
10 41 00 00 7A 44 00 00 20 41 B9 6C 3E 3E
```

| 整帧偏移 | 十六进制字节 | 字段 | 含义 |
|---:|---|---|---|
| 0～1 | `3C 3C` | Header | 固定帧头 |
| 2 | `07` | Channel | `0x07`，标定通道 |
| 3 | `09` | Flags | PackageId=2,请求帧 |
| 4～5 | `64 00` | PayloadLength | 100 字节，小端 |
| 6 | `01` | Protocol | 固定 `0x01` |
| 7 | `11` | Command | `0x11` |
| 8 | `03` | Endpoint | `0x03` |
| 9 | `01` | Region | `0x01` |
| 10～11 | `03 00` | Sequence | `0x0003`，小端 |
| 12～13 | `56 34` | Session | `0x3456`，小端 |
| 14～15 | `58 00` | DataLength | 88 字节，小端 |
| 16～17 | `00 00` | Reserved | 固定 0 |
| 18 | `00` | First | 首点下标 0 |
| 19 | `0A` | Count | 10 点 |
| 20～21 | `50 00` | ChunkBytes | 80 字节 |
| 22～25 | `CF 5D C4 75` | ChunkCRC32 | `0x75C45DCF` |
| 26～29 | `00 00 C8 42` | Point[0].AD | 100.0f |
| 30～33 | `00 00 80 3F` | Point[0].Force | 1.0f g |
| 34～37 | `00 00 48 43` | Point[1].AD | 200.0f |
| 38～41 | `00 00 00 40` | Point[1].Force | 2.0f g |
| 42～45 | `00 00 96 43` | Point[2].AD | 300.0f |
| 46～49 | `00 00 40 40` | Point[2].Force | 3.0f g |
| 50～53 | `00 00 C8 43` | Point[3].AD | 400.0f |
| 54～57 | `00 00 80 40` | Point[3].Force | 4.0f g |
| 58～61 | `00 00 FA 43` | Point[4].AD | 500.0f |
| 62～65 | `00 00 A0 40` | Point[4].Force | 5.0f g |
| 66～69 | `00 00 16 44` | Point[5].AD | 600.0f |
| 70～73 | `00 00 C0 40` | Point[5].Force | 6.0f g |
| 74～77 | `00 00 2F 44` | Point[6].AD | 700.0f |
| 78～81 | `00 00 E0 40` | Point[6].Force | 7.0f g |
| 82～85 | `00 00 48 44` | Point[7].AD | 800.0f |
| 86～89 | `00 00 00 41` | Point[7].Force | 8.0f g |
| 90～93 | `00 00 61 44` | Point[8].AD | 900.0f |
| 94～97 | `00 00 10 41` | Point[8].Force | 9.0f g |
| 98～101 | `00 00 7A 44` | Point[9].AD | 1000.0f |
| 102～105 | `00 00 20 41` | Point[9].Force | 10.0f g |
| 106～107 | `B9 6C` | CRC16 | `0x6CB9`，只计算 Payload |
| 108～109 | `3E 3E` | Tail | 固定帧尾 |

应答帧，共 34 字节：

```text
3C 3C 07 0A 18 00 01 91 03 01 03 00 56 34 0C 00
00 00 00 0A 0A 00 CF 5D C4 75 11 09 26 20 22 23
3E 3E
```

| 整帧偏移 | 十六进制字节 | 字段 | 含义 |
|---:|---|---|---|
| 0～1 | `3C 3C` | Header | 固定帧头 |
| 2 | `07` | Channel | `0x07`，标定通道 |
| 3 | `0A` | Flags | PackageId=2,应答帧 |
| 4～5 | `18 00` | PayloadLength | 24 字节，小端 |
| 6 | `01` | Protocol | 固定 `0x01` |
| 7 | `91` | Command | `0x91` |
| 8 | `03` | Endpoint | `0x03` |
| 9 | `01` | Region | `0x01` |
| 10～11 | `03 00` | Sequence | `0x0003`，小端 |
| 12～13 | `56 34` | Session | `0x3456`，小端 |
| 14～15 | `0C 00` | DataLength | 12 字节，小端 |
| 16～17 | `00 00` | Reserved | 固定 0 |
| 18 | `00` | Status | `0x00`，成功 |
| 19 | `0A` | PointCount | 10 |
| 20 | `0A` | ReceivedCount | 10 |
| 21 | `00` | Mode | `0x00` 原始 AD |
| 22～25 | `CF 5D C4 75` | TableCRC32 | `0x75C45DCF`，小端 |
| 26～29 | `11 09 26 20` | DataId | `0x20260911`，小端 |
| 30～31 | `22 23` | CRC16 | `0x2322`，只计算 Payload |
| 32～33 | `3E 3E` | Tail | 固定帧尾 |
### VERIFY

作用：检查数据是否收齐、CRC32、AD 递增、Force 单调以及浮点数有效性。

请求帧，共 22 字节：

```text
3C 3C 07 0D 0C 00 01 12 03 01 04 00 56 34 00 00
00 00 E3 25 3E 3E
```

| 整帧偏移 | 十六进制字节 | 字段 | 含义 |
|---:|---|---|---|
| 0～1 | `3C 3C` | Header | 固定帧头 |
| 2 | `07` | Channel | `0x07`，标定通道 |
| 3 | `0D` | Flags | PackageId=3,请求帧 |
| 4～5 | `0C 00` | PayloadLength | 12 字节，小端 |
| 6 | `01` | Protocol | 固定 `0x01` |
| 7 | `12` | Command | `0x12` |
| 8 | `03` | Endpoint | `0x03` |
| 9 | `01` | Region | `0x01` |
| 10～11 | `04 00` | Sequence | `0x0004`，小端 |
| 12～13 | `56 34` | Session | `0x3456`，小端 |
| 14～15 | `00 00` | DataLength | 0 字节，小端 |
| 16～17 | `00 00` | Reserved | 固定 0 |
| 18～19 | `E3 25` | CRC16 | `0x25E3`，只计算 Payload |
| 20～21 | `3E 3E` | Tail | 固定帧尾 |

应答帧，共 34 字节：

```text
3C 3C 07 0E 18 00 01 92 03 01 04 00 56 34 0C 00
00 00 00 0A 0A 00 CF 5D C4 75 11 09 26 20 C3 71
3E 3E
```

| 整帧偏移 | 十六进制字节 | 字段 | 含义 |
|---:|---|---|---|
| 0～1 | `3C 3C` | Header | 固定帧头 |
| 2 | `07` | Channel | `0x07`，标定通道 |
| 3 | `0E` | Flags | PackageId=3,应答帧 |
| 4～5 | `18 00` | PayloadLength | 24 字节，小端 |
| 6 | `01` | Protocol | 固定 `0x01` |
| 7 | `92` | Command | `0x92` |
| 8 | `03` | Endpoint | `0x03` |
| 9 | `01` | Region | `0x01` |
| 10～11 | `04 00` | Sequence | `0x0004`，小端 |
| 12～13 | `56 34` | Session | `0x3456`，小端 |
| 14～15 | `0C 00` | DataLength | 12 字节，小端 |
| 16～17 | `00 00` | Reserved | 固定 0 |
| 18 | `00` | Status | `0x00`，成功 |
| 19 | `0A` | PointCount | 10 |
| 20 | `0A` | ReceivedCount | 10 |
| 21 | `00` | Mode | `0x00` 原始 AD |
| 22～25 | `CF 5D C4 75` | TableCRC32 | `0x75C45DCF`，小端 |
| 26～29 | `11 09 26 20` | DataId | `0x20260911`，小端 |
| 30～31 | `C3 71` | CRC16 | `0x71C3`，只计算 Payload |
| 32～33 | `3E 3E` | Tail | 固定帧尾 |
### APPLY

作用：确认已经通过 VERIFY 的暂存数据允许提交。

请求帧，共 26 字节：

```text
3C 3C 07 11 10 00 01 13 03 01 05 00 56 34 04 00
00 00 01 02 00 00 BB 43 3E 3E
```

| 整帧偏移 | 十六进制字节 | 字段 | 含义 |
|---:|---|---|---|
| 0～1 | `3C 3C` | Header | 固定帧头 |
| 2 | `07` | Channel | `0x07`，标定通道 |
| 3 | `11` | Flags | PackageId=4,请求帧 |
| 4～5 | `10 00` | PayloadLength | 16 字节，小端 |
| 6 | `01` | Protocol | 固定 `0x01` |
| 7 | `13` | Command | `0x13` |
| 8 | `03` | Endpoint | `0x03` |
| 9 | `01` | Region | `0x01` |
| 10～11 | `05 00` | Sequence | `0x0005`，小端 |
| 12～13 | `56 34` | Session | `0x3456`，小端 |
| 14～15 | `04 00` | DataLength | 4 字节，小端 |
| 16～17 | `00 00` | Reserved | 固定 0 |
| 18 | `01` | Apply | `0x01` |
| 19 | `02` | TargetMode | `0x02` |
| 20～21 | `00 00` | Reserved | 固定 0 |
| 22～23 | `BB 43` | CRC16 | `0x43BB`，只计算 Payload |
| 24～25 | `3E 3E` | Tail | 固定帧尾 |

应答帧，共 34 字节：

```text
3C 3C 07 12 18 00 01 93 03 01 05 00 56 34 0C 00
00 00 00 0A 0A 00 CF 5D C4 75 11 09 26 20 33 3C
3E 3E
```

| 整帧偏移 | 十六进制字节 | 字段 | 含义 |
|---:|---|---|---|
| 0～1 | `3C 3C` | Header | 固定帧头 |
| 2 | `07` | Channel | `0x07`，标定通道 |
| 3 | `12` | Flags | PackageId=4,应答帧 |
| 4～5 | `18 00` | PayloadLength | 24 字节，小端 |
| 6 | `01` | Protocol | 固定 `0x01` |
| 7 | `93` | Command | `0x93` |
| 8 | `03` | Endpoint | `0x03` |
| 9 | `01` | Region | `0x01` |
| 10～11 | `05 00` | Sequence | `0x0005`，小端 |
| 12～13 | `56 34` | Session | `0x3456`，小端 |
| 14～15 | `0C 00` | DataLength | 12 字节，小端 |
| 16～17 | `00 00` | Reserved | 固定 0 |
| 18 | `00` | Status | `0x00`，成功 |
| 19 | `0A` | PointCount | 10 |
| 20 | `0A` | ReceivedCount | 10 |
| 21 | `00` | Mode | `0x00` 原始 AD |
| 22～25 | `CF 5D C4 75` | TableCRC32 | `0x75C45DCF`，小端 |
| 26～29 | `11 09 26 20` | DataId | `0x20260911`，小端 |
| 30～31 | `33 3C` | CRC16 | `0x3C33`，只计算 Payload |
| 32～33 | `3E 3E` | Tail | 固定帧尾 |
### COMMIT

作用：将 LUT 写入 Flash，回读校验成功后替换旧 LUT。

请求帧，共 34 字节：

```text
3C 3C 07 15 18 00 01 14 03 01 06 00 56 34 0C 00
00 00 CF 5D C4 75 11 09 26 20 01 00 00 00 06 E5
3E 3E
```

| 整帧偏移 | 十六进制字节 | 字段 | 含义 |
|---:|---|---|---|
| 0～1 | `3C 3C` | Header | 固定帧头 |
| 2 | `07` | Channel | `0x07`，标定通道 |
| 3 | `15` | Flags | PackageId=5,请求帧 |
| 4～5 | `18 00` | PayloadLength | 24 字节，小端 |
| 6 | `01` | Protocol | 固定 `0x01` |
| 7 | `14` | Command | `0x14` |
| 8 | `03` | Endpoint | `0x03` |
| 9 | `01` | Region | `0x01` |
| 10～11 | `06 00` | Sequence | `0x0006`，小端 |
| 12～13 | `56 34` | Session | `0x3456`，小端 |
| 14～15 | `0C 00` | DataLength | 12 字节，小端 |
| 16～17 | `00 00` | Reserved | 固定 0 |
| 18～21 | `CF 5D C4 75` | TableCRC32 | `0x75C45DCF` |
| 22～25 | `11 09 26 20` | DataId | `0x20260911` |
| 26 | `01` | Enabled | `0x01` |
| 27～29 | `00 00 00` | Reserved | 固定 0 |
| 30～31 | `06 E5` | CRC16 | `0xE506`，只计算 Payload |
| 32～33 | `3E 3E` | Tail | 固定帧尾 |

应答帧，共 34 字节：

```text
3C 3C 07 16 18 00 01 94 03 01 06 00 56 34 0C 00
00 00 00 0A 0A 00 CF 5D C4 75 11 09 26 20 11 19
3E 3E
```

| 整帧偏移 | 十六进制字节 | 字段 | 含义 |
|---:|---|---|---|
| 0～1 | `3C 3C` | Header | 固定帧头 |
| 2 | `07` | Channel | `0x07`，标定通道 |
| 3 | `16` | Flags | PackageId=5,应答帧 |
| 4～5 | `18 00` | PayloadLength | 24 字节，小端 |
| 6 | `01` | Protocol | 固定 `0x01` |
| 7 | `94` | Command | `0x94` |
| 8 | `03` | Endpoint | `0x03` |
| 9 | `01` | Region | `0x01` |
| 10～11 | `06 00` | Sequence | `0x0006`，小端 |
| 12～13 | `56 34` | Session | `0x3456`，小端 |
| 14～15 | `0C 00` | DataLength | 12 字节，小端 |
| 16～17 | `00 00` | Reserved | 固定 0 |
| 18 | `00` | Status | `0x00`，成功 |
| 19 | `0A` | PointCount | 10 |
| 20 | `0A` | ReceivedCount | 10 |
| 21 | `00` | Mode | `0x00` 原始 AD |
| 22～25 | `CF 5D C4 75` | TableCRC32 | `0x75C45DCF`，小端 |
| 26～29 | `11 09 26 20` | DataId | `0x20260911`，小端 |
| 30～31 | `11 19` | CRC16 | `0x1911`，只计算 Payload |
| 32～33 | `3E 3E` | Tail | 固定帧尾 |
### STATUS（下发后核验）

作用：从 Flash 查询 0x31，核对点数、CRC32 和 DataId。

请求帧，共 22 字节：

```text
3C 3C 07 19 0C 00 01 02 03 01 07 00 00 00 00 00
00 00 4A 02 3E 3E
```

| 整帧偏移 | 十六进制字节 | 字段 | 含义 |
|---:|---|---|---|
| 0～1 | `3C 3C` | Header | 固定帧头 |
| 2 | `07` | Channel | `0x07`，标定通道 |
| 3 | `19` | Flags | PackageId=6,请求帧 |
| 4～5 | `0C 00` | PayloadLength | 12 字节，小端 |
| 6 | `01` | Protocol | 固定 `0x01` |
| 7 | `02` | Command | `0x02` |
| 8 | `03` | Endpoint | `0x03` |
| 9 | `01` | Region | `0x01` |
| 10～11 | `07 00` | Sequence | `0x0007`，小端 |
| 12～13 | `00 00` | Session | `0x0000`，小端 |
| 14～15 | `00 00` | DataLength | 0 字节，小端 |
| 16～17 | `00 00` | Reserved | 固定 0 |
| 18～19 | `4A 02` | CRC16 | `0x024A`，只计算 Payload |
| 20～21 | `3E 3E` | Tail | 固定帧尾 |

应答帧，共 34 字节：

```text
3C 3C 07 1A 18 00 01 82 03 01 07 00 00 00 0C 00
00 00 00 0A 0A 00 CF 5D C4 75 11 09 26 20 ED 18
3E 3E
```

| 整帧偏移 | 十六进制字节 | 字段 | 含义 |
|---:|---|---|---|
| 0～1 | `3C 3C` | Header | 固定帧头 |
| 2 | `07` | Channel | `0x07`，标定通道 |
| 3 | `1A` | Flags | PackageId=6,应答帧 |
| 4～5 | `18 00` | PayloadLength | 24 字节，小端 |
| 6 | `01` | Protocol | 固定 `0x01` |
| 7 | `82` | Command | `0x82` |
| 8 | `03` | Endpoint | `0x03` |
| 9 | `01` | Region | `0x01` |
| 10～11 | `07 00` | Sequence | `0x0007`，小端 |
| 12～13 | `00 00` | Session | `0x0000`，小端 |
| 14～15 | `0C 00` | DataLength | 12 字节，小端 |
| 16～17 | `00 00` | Reserved | 固定 0 |
| 18 | `00` | Status | `0x00`，成功 |
| 19 | `0A` | PointCount | 10 |
| 20 | `0A` | ReceivedCount | 10 |
| 21 | `00` | Mode | `0x00` 原始 AD |
| 22～25 | `CF 5D C4 75` | TableCRC32 | `0x75C45DCF`，小端 |
| 26～29 | `11 09 26 20` | DataId | `0x20260911`，小端 |
| 30～31 | `ED 18` | CRC16 | `0x18ED`，只计算 Payload |
| 32～33 | `3E 3E` | Tail | 固定帧尾 |
## 辅助命令完整问答

ABORT 是异常流程；STATUS_ALL、ERASE 和 SET_MODE 独立使用，不属于 LUT 写入的七步流程。

### STATUS_ALL

作用：一次查询 14 个区域的标定状态。请求头的 Endpoint、Region 均填 `0x00`，DataLength=0。

请求帧，共 22 字节：

```text
3C 3C 07 01 0C 00 01 03 00 00 08 00 00 00 00 00
00 00 F3 21 3E 3E
```

| 整帧偏移 | 十六进制字节 | 字段 | 含义 |
|---:|---|---|---|
| 0～1 | `3C 3C` | Header | 固定帧头 |
| 2 | `07` | Channel | `0x07`，标定通道 |
| 3 | `01` | Flags | PackageId=0,请求帧 |
| 4～5 | `0C 00` | PayloadLength | 12 字节，小端 |
| 6 | `01` | Protocol | 固定 `0x01` |
| 7 | `03` | Command | `0x03` |
| 8 | `00` | Endpoint | 固定 `0x00` |
| 9 | `00` | Region | 固定 `0x00` |
| 10～11 | `08 00` | Sequence | `0x0008`，小端 |
| 12～13 | `00 00` | Session | `0x0000`，小端 |
| 14～15 | `00 00` | DataLength | 0 字节，小端 |
| 16～17 | `00 00` | Reserved | 固定 0 |
| 18～19 | `F3 21` | CRC16 | `0x21F3`，只计算 Payload |
| 20～21 | `3E 3E` | Tail | 固定帧尾 |

应答数据仍为 12 字节，本命令的字段含义为：

```text
Status:u8 | RegionCount:u8 | CalibratedMask:u16 | ForceModeMask:u16 | Reserved[6]
```

- RegionCount 固定为 14（`0x0E`）。
- CalibratedMask：已标定（LUT 已写入 Flash）的区域，位序见"区域状态掩码位序"。
- ForceModeMask：处于力值模式的区域，位序相同。
- Reserved 恒为 0。

应答帧示例一（14 个区域全部已标定、全部处于力值模式），共 34 字节：

```text
3C 3C 07 02 18 00 01 83 00 00 08 00 00 00 0C 00
00 00 00 0E FF 3F FF 3F 00 00 00 00 00 00 11 B7
3E 3E
```

| 整帧偏移 | 十六进制字节 | 字段 | 含义 |
|---:|---|---|---|
| 0～1 | `3C 3C` | Header | 固定帧头 |
| 2 | `07` | Channel | `0x07`，标定通道 |
| 3 | `02` | Flags | PackageId=0,应答帧 |
| 4～5 | `18 00` | PayloadLength | 24 字节，小端 |
| 6 | `01` | Protocol | 固定 `0x01` |
| 7 | `83` | Command | `0x83` |
| 8 | `00` | Endpoint | `0x00` |
| 9 | `00` | Region | `0x00` |
| 10～11 | `08 00` | Sequence | `0x0008`，小端 |
| 12～13 | `00 00` | Session | `0x0000`，小端 |
| 14～15 | `0C 00` | DataLength | 12 字节，小端 |
| 16～17 | `00 00` | Reserved | 固定 0 |
| 18 | `00` | Status | `0x00`，成功 |
| 19 | `0E` | RegionCount | 14 |
| 20～21 | `FF 3F` | CalibratedMask | `0x3FFF`，14 个区域全部已标定 |
| 22～23 | `FF 3F` | ForceModeMask | `0x3FFF`，全部处于力值模式 |
| 24～29 | `00 00 00 00 00 00` | Reserved | 固定 0 |
| 30～31 | `11 B7` | CRC16 | `0xB711`，只计算 Payload |
| 32～33 | `3E 3E` | Tail | 固定帧尾 |

应答帧示例二（只有中指指中 `0x32` 未标定，即 bit6 为 0，掩码 `0x3FBF`）：

```text
3C 3C 07 02 18 00 01 83 00 00 08 00 00 00 0C 00
00 00 00 0E BF 3F BF 3F 00 00 00 00 00 00 17 53
3E 3E
```

### ABORT

作用：取消尚未完成的事务，只清除 RAM 暂存数据。

请求帧，共 22 字节：

```text
3C 3C 07 0D 0C 00 01 16 03 01 04 00 56 34 00 00
00 00 F6 15 3E 3E
```

| 整帧偏移 | 十六进制字节 | 字段 | 含义 |
|---:|---|---|---|
| 0～1 | `3C 3C` | Header | 固定帧头 |
| 2 | `07` | Channel | `0x07`，标定通道 |
| 3 | `0D` | Flags | PackageId=3,请求帧 |
| 4～5 | `0C 00` | PayloadLength | 12 字节，小端 |
| 6 | `01` | Protocol | 固定 `0x01` |
| 7 | `16` | Command | `0x16` |
| 8 | `03` | Endpoint | `0x03` |
| 9 | `01` | Region | `0x01` |
| 10～11 | `04 00` | Sequence | `0x0004`，小端 |
| 12～13 | `56 34` | Session | `0x3456`，小端 |
| 14～15 | `00 00` | DataLength | 0 字节，小端 |
| 16～17 | `00 00` | Reserved | 固定 0 |
| 18～19 | `F6 15` | CRC16 | `0x15F6`，只计算 Payload |
| 20～21 | `3E 3E` | Tail | 固定帧尾 |

应答帧，共 34 字节：

```text
3C 3C 07 0E 18 00 01 96 03 01 04 00 56 34 0C 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 63 66
3E 3E
```

| 整帧偏移 | 十六进制字节 | 字段 | 含义 |
|---:|---|---|---|
| 0～1 | `3C 3C` | Header | 固定帧头 |
| 2 | `07` | Channel | `0x07`，标定通道 |
| 3 | `0E` | Flags | PackageId=3,应答帧 |
| 4～5 | `18 00` | PayloadLength | 24 字节，小端 |
| 6 | `01` | Protocol | 固定 `0x01` |
| 7 | `96` | Command | `0x96` |
| 8 | `03` | Endpoint | `0x03` |
| 9 | `01` | Region | `0x01` |
| 10～11 | `04 00` | Sequence | `0x0004`，小端 |
| 12～13 | `56 34` | Session | `0x3456`，小端 |
| 14～15 | `0C 00` | DataLength | 12 字节，小端 |
| 16～17 | `00 00` | Reserved | 固定 0 |
| 18 | `00` | Status | `0x00`，成功 |
| 19 | `00` | PointCount | 0 |
| 20 | `00` | ReceivedCount | 0 |
| 21 | `00` | Mode | `0x00` 原始 AD |
| 22～25 | `00 00 00 00` | TableCRC32 | `0x00000000`，小端 |
| 26～29 | `00 00 00 00` | DataId | `0x00000000`，小端 |
| 30～31 | `63 66` | CRC16 | `0x6663`，只计算 Payload |
| 32～33 | `3E 3E` | Tail | 固定帧尾 |
### ERASE

作用：清除标定数据。正常 LUT 更新不使用此命令。擦除范围由可选的 Scope 字节决定：

| DataLength | Scope | 擦除范围 |
|---:|---:|---|
| 0 | — | 整根手指（Endpoint 下的全部区域），Region 字段忽略 |
| 1 | `0x00` | 整根手指，同上 |
| 1 | `0x01` | 只擦除请求头 Endpoint + Region 指定的这一个区域 |

Scope 为其它值时返回 `0x03`。擦除后对应区域的"已标定"标志清零，全局"是否标定"随之变为 `0x00`。

#### 示例一：擦除整根手指

清除 Endpoint=0x03 的全部区域。请求帧，共 22 字节：

```text
3C 3C 07 21 0C 00 01 17 03 01 30 00 00 00 00 00
00 00 4D FC 3E 3E
```

| 整帧偏移 | 十六进制字节 | 字段 | 含义 |
|---:|---|---|---|
| 0～1 | `3C 3C` | Header | 固定帧头 |
| 2 | `07` | Channel | `0x07`，标定通道 |
| 3 | `21` | Flags | PackageId=8,请求帧 |
| 4～5 | `0C 00` | PayloadLength | 12 字节，小端 |
| 6 | `01` | Protocol | 固定 `0x01` |
| 7 | `17` | Command | `0x17` |
| 8 | `03` | Endpoint | `0x03` |
| 9 | `01` | Region | `0x01` |
| 10～11 | `30 00` | Sequence | `0x0030`，小端 |
| 12～13 | `00 00` | Session | `0x0000`，小端 |
| 14～15 | `00 00` | DataLength | 0 字节，小端 |
| 16～17 | `00 00` | Reserved | 固定 0 |
| 18～19 | `4D FC` | CRC16 | `0xFC4D`，只计算 Payload |
| 20～21 | `3E 3E` | Tail | 固定帧尾 |

应答帧，共 34 字节：

```text
3C 3C 07 22 18 00 01 97 03 01 30 00 00 00 0C 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 BD DC
3E 3E
```

| 整帧偏移 | 十六进制字节 | 字段 | 含义 |
|---:|---|---|---|
| 0～1 | `3C 3C` | Header | 固定帧头 |
| 2 | `07` | Channel | `0x07`，标定通道 |
| 3 | `22` | Flags | PackageId=8,应答帧 |
| 4～5 | `18 00` | PayloadLength | 24 字节，小端 |
| 6 | `01` | Protocol | 固定 `0x01` |
| 7 | `97` | Command | `0x97` |
| 8 | `03` | Endpoint | `0x03` |
| 9 | `01` | Region | `0x01` |
| 10～11 | `30 00` | Sequence | `0x0030`，小端 |
| 12～13 | `00 00` | Session | `0x0000`，小端 |
| 14～15 | `0C 00` | DataLength | 12 字节，小端 |
| 16～17 | `00 00` | Reserved | 固定 0 |
| 18 | `00` | Status | `0x00`，成功 |
| 19 | `00` | PointCount | 0 |
| 20 | `00` | ReceivedCount | 0 |
| 21 | `00` | Mode | `0x00` 原始 AD |
| 22～25 | `00 00 00 00` | TableCRC32 | `0x00000000`，小端 |
| 26～29 | `00 00 00 00` | DataId | `0x00000000`，小端 |
| 30～31 | `BD DC` | CRC16 | `0xDCBD`，只计算 Payload |
| 32～33 | `3E 3E` | Tail | 固定帧尾 |

#### 示例二：只擦除一个区域

只清除中指指中（Endpoint=0x03、Region=0x02），其它区域不受影响。请求帧，共 23 字节：

```text
3C 3C 07 05 0D 00 01 17 03 02 09 00 00 00 01 00
00 00 01 0E 6B 3E 3E
```

| 整帧偏移 | 十六进制字节 | 字段 | 含义 |
|---:|---|---|---|
| 0～1 | `3C 3C` | Header | 固定帧头 |
| 2 | `07` | Channel | `0x07`，标定通道 |
| 3 | `05` | Flags | PackageId=1,请求帧 |
| 4～5 | `0D 00` | PayloadLength | 13 字节，小端 |
| 6 | `01` | Protocol | 固定 `0x01` |
| 7 | `17` | Command | `0x17` |
| 8 | `03` | Endpoint | `0x03` |
| 9 | `02` | Region | `0x02` |
| 10～11 | `09 00` | Sequence | `0x0009`，小端 |
| 12～13 | `00 00` | Session | `0x0000`，小端 |
| 14～15 | `01 00` | DataLength | 1 字节，小端 |
| 16～17 | `00 00` | Reserved | 固定 0 |
| 18 | `01` | Scope | `0x01`，只擦除本区域 |
| 19～20 | `0E 6B` | CRC16 | `0x6B0E`，只计算 Payload |
| 21～22 | `3E 3E` | Tail | 固定帧尾 |

应答帧，共 34 字节：

```text
3C 3C 07 06 18 00 01 97 03 02 09 00 00 00 0C 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 17 83
3E 3E
```

应答字段与示例一相同，Endpoint/Region 回显 `03 02`。
### SET_MODE

作用：设置 Endpoint=0x03 进入力值上传模式。

请求帧，共 23 字节：

```text
3C 3C 07 25 0D 00 01 18 03 01 31 00 00 00 01 00
00 00 02 99 B0 3E 3E
```

| 整帧偏移 | 十六进制字节 | 字段 | 含义 |
|---:|---|---|---|
| 0～1 | `3C 3C` | Header | 固定帧头 |
| 2 | `07` | Channel | `0x07`，标定通道 |
| 3 | `25` | Flags | PackageId=9,请求帧 |
| 4～5 | `0D 00` | PayloadLength | 13 字节，小端 |
| 6 | `01` | Protocol | 固定 `0x01` |
| 7 | `18` | Command | `0x18` |
| 8 | `03` | Endpoint | `0x03` |
| 9 | `01` | Region | `0x01` |
| 10～11 | `31 00` | Sequence | `0x0031`，小端 |
| 12～13 | `00 00` | Session | `0x0000`，小端 |
| 14～15 | `01 00` | DataLength | 1 字节，小端 |
| 16～17 | `00 00` | Reserved | 固定 0 |
| 18 | `02` | Mode | `0x02`，力值模式 |
| 19～20 | `99 B0` | CRC16 | `0xB099`，只计算 Payload |
| 21～22 | `3E 3E` | Tail | 固定帧尾 |

应答帧，共 34 字节：

```text
3C 3C 07 26 18 00 01 98 03 01 31 00 00 00 0C 00
00 00 00 0A 0A 02 CF 5D C4 75 11 09 26 20 0A 39
3E 3E
```

| 整帧偏移 | 十六进制字节 | 字段 | 含义 |
|---:|---|---|---|
| 0～1 | `3C 3C` | Header | 固定帧头 |
| 2 | `07` | Channel | `0x07`，标定通道 |
| 3 | `26` | Flags | PackageId=9,应答帧 |
| 4～5 | `18 00` | PayloadLength | 24 字节，小端 |
| 6 | `01` | Protocol | 固定 `0x01` |
| 7 | `98` | Command | `0x98` |
| 8 | `03` | Endpoint | `0x03` |
| 9 | `01` | Region | `0x01` |
| 10～11 | `31 00` | Sequence | `0x0031`，小端 |
| 12～13 | `00 00` | Session | `0x0000`，小端 |
| 14～15 | `0C 00` | DataLength | 12 字节，小端 |
| 16～17 | `00 00` | Reserved | 固定 0 |
| 18 | `00` | Status | `0x00`，成功 |
| 19 | `0A` | PointCount | 10 |
| 20 | `0A` | ReceivedCount | 10 |
| 21 | `02` | Mode | `0x02` 力值 |
| 22～25 | `CF 5D C4 75` | TableCRC32 | `0x75C45DCF`，小端 |
| 26～29 | `11 09 26 20` | DataId | `0x20260911`，小端 |
| 30～31 | `0A 39` | CRC16 | `0x390A`，只计算 Payload |
| 32～33 | `3E 3E` | Tail | 固定帧尾 |
## 状态判断

下发后的 STATUS 满足以下条件才判定成功：

```text
Status     == 0x00
PointCount == 10
TableCRC32 == 0x75C45DCF
DataId     == 0x20260911
```

COMMIT 应答超时时，先发送 STATUS。STATUS 的 PointCount、TableCRC32、DataId 全部一致，可判定 Flash 实际提交成功。

主要错误码：

| Status | 含义 |
|---:|---|
| `0x00` | 成功 |
| `0x03` | 参数、长度或区域错误 |
| `0x04` | Session 不匹配 |
| `0x08` | CRC 错误 |
| `0x09` | 数据未收齐 |
| `0x0C` | AD 或 Force 不单调 |
| `0x0E` | 未完成 VERIFY/APPLY |
| `0x10` | Flash 擦写或回读失败 |
| `0x11` | 事务忙 |
| `0x12` | 区域数据不完整 |

## CRC 源码

```c
#include <stddef.h>
#include <stdint.h>

/* 外层帧 CRC16。输入范围仅为 Payload。 */
uint16_t tactile_crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0x0000U;
    while (length-- > 0U) {
        crc ^= (uint16_t)(*data++);
        for (uint8_t i = 0U; i < 8U; ++i) {
            crc = (crc & 1U) ? (uint16_t)((crc >> 1U) ^ 0xA001U)
                             : (uint16_t)(crc >> 1U);
        }
    }
    return crc;
}

/* LUT 整表和 WRITE 数据块 CRC32。 */
uint32_t tactile_crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xFFFFFFFFUL;
    while (length-- > 0U) {
        crc ^= (uint32_t)(*data++);
        for (uint8_t i = 0U; i < 8U; ++i) {
            crc = (crc & 1UL) ? ((crc >> 1U) ^ 0xEDB88320UL)
                              : (crc >> 1U);
        }
    }
    return crc ^ 0xFFFFFFFFUL;
}
```

CRC 按小端发送。例如 `0x75C45DCF` 发送为 `CF 5D C4 75`。

## MCU 实现要求

1. 使用 Endpoint+Region 作为 LUT 的 RAM 和 Flash 索引。
2. BEGIN～COMMIT 必须校验同一个非零 Session。
3. 相同 WRITE 重发时不得重复累计 ReceivedCount。
4. VERIFY 成功前不得 APPLY，APPLY 成功前不得 COMMIT。
5. COMMIT 完成 Flash 写入、回读和校验后才能应答成功。
6. 更新一个区域时不得擦除同部位的其他区域。
7. STATUS 必须读取已提交的有效记录，不能返回未提交的 RAM 暂存数据。
8. 每个区域的"已标定"标志随 LUT 保存在 Flash；STATUS_ALL 按固定位序返回 14 个区域的掩码。
9. ERASE 的 DataLength=0 时擦除整根手指，与旧上位机兼容；带 Scope=0x01 时只擦除指定区域。
10. 某次 COMMIT 让 14 个区域全部标定完成时，必须先把该 COMMIT 的应答完整发出，再复位设备。
11. 擦除和写入 Flash 期间必须关闭中断：Flash 不可读，而中断向量和中断服务函数都在 Flash 中。
