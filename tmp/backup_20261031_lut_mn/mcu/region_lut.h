#ifndef APPLICATION_REGION_LUT_H
#define APPLICATION_REGION_LUT_H

#include "serial.h"

#include <stdint.h>

/*
 * 分区域 AD->Force 标定表，协议见 Doc/BRAINCO_REGION_LUT_DEBUG_PROTOCOL.md。
 *
 * 每个 (Endpoint, Region) 一条单调曲线。AD 轴使用实时上传通道同一套 8 位量程
 * （raw_adc >> 4、扣零点之后），因此标定采集必须在同一量程下进行。
 */

#define REGION_LUT_MAX_POINTS     64U
#define REGION_LUT_ENDPOINT_COUNT 5U
#define REGION_LUT_REGION_COUNT   3U
#define REGION_LUT_SLOT_COUNT     (REGION_LUT_ENDPOINT_COUNT * REGION_LUT_REGION_COUNT)

#define REGION_LUT_MODE_RAW_AD 0x00U
#define REGION_LUT_MODE_FORCE  0x02U

/*
 * 14 个区域的状态掩码（STATUS_ALL 应答用），位序固定：
 *   bit0 0x11 拇指指尖   bit1 0x12 拇指指根
 *   bit2~4  0x21~0x23 食指   bit5~7  0x31~0x33 中指
 *   bit8~10 0x41~0x43 无名指 bit11~13 0x51~0x53 小指（各为指尖/指中/指根）
 */
#define REGION_LUT_REGION_TOTAL 14U
#define REGION_LUT_ALL_MASK     0x3FFFU

void region_lut_init(void);

/* CHANNEL=0x07 且 flags 低两位为请求位时调用，返回 1 表示该帧已由本模块处理。 */
uint8_t region_lut_handle_request(package_t *package);

/* 已写入标定表（valid）的区域掩码；等于 REGION_LUT_ALL_MASK 即 14 个区域全部标定完成。 */
uint16_t region_lut_calibrated_mask(void);

/* 处于力值模式的区域掩码。 */
uint16_t region_lut_force_mask(void);

/* 某次 COMMIT 让 14 个区域全部完成后置 1，由主循环在应答发送完后复位设备。 */
uint8_t region_lut_reboot_pending(void);

/*
 * 对一根手指的采样矩阵按区域应用标定：区域 mode 为力值时，用该区域有效点的 AD 总和
 * 查表插值出总力，再按各点 AD 占比分摊回每个点；mode 为原始 AD 时该区域不变。
 */
void region_lut_apply_to_rect(uint8_t rect_id, uint16_t *data, uint8_t stride, uint16_t output_limit);

#endif // APPLICATION_REGION_LUT_H
