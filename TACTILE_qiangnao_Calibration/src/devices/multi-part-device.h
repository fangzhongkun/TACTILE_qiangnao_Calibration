#ifndef MULTI_PART_DEVICE_H
#define MULTI_PART_DEVICE_H

#include "device.h"

#include <map>
#include <memory>

/**
 * @brief 多部位设备管理器
 *
 * 用于管理主机(XMST)连接的多个从机设备（五指）
 * 每个从机通过不同的地址(slave_id)进行寻址
 */
class MultiPartDevice : public Device
{
public:
    explicit MultiPartDevice(serial::options options);
    ~MultiPartDevice() override = default;

    int     open() override;
    size_t  get_finger_size() override { return 5; }
    uint8_t get_id() override { return 0; }

    /**
     * @brief 设置当前活动的从机地址
     * @param part_id 部位ID (1-5)
     */
    void set_active_part(uint8_t part_id);

    /**
     * @brief 获取当前活动的从机地址
     */
    uint8_t get_active_part() const { return active_part_; }

    /**
     * @brief 扫描并发现所有连接的从机
     * @return 找到的从机地址列表
     */
    std::vector<uint8_t> scan_parts();

    /**
     * @brief 为指定部位下发标定曲线
     * @param part_id 部位ID
     * @param buf LUT数据
     * @param count 数据点数
     * @param package_id 包ID (0-4)
     */
    void calibration_curve_for_part(uint8_t part_id, float *buf, size_t count, int package_id);

    /**
     * @brief 查询指定部位的标定状态
     * @param part_id 部位ID
     * @return 标定状态码
     */
    uint8_t calibration_query_for_part(uint8_t part_id);

    /**
     * @brief 为指定部位设置零点参数
     * @param part_id 部位ID
     * @param zeroing 零点参数
     */
    int zeroing_for_part(uint8_t part_id, const touch::zeroing& zeroing);

    /**
     * @brief 启用/禁用指定部位的零点校准
     * @param part_id 部位ID
     * @param enable true=启用, false=禁用
     */
    int enable_zeroing_for_part(uint8_t part_id, bool enable);

    /**
     * @brief 从所有部位读取数据帧
     * @return 部位ID到矩阵的映射
     */
    std::map<uint8_t, touch::matrix<uint16_t>> read_all_parts_frames();

    /**
     * @brief 获取所有部位的在线状态
     * @return 部位ID到在线状态的映射
     */
    std::map<uint8_t, bool> get_parts_online_status();

    // 各部位数据到达回调
    std::function<void(uint8_t part_id, const touch::matrix<uint16_t>&)> onpart_arrived = [](auto&&...) {};

    // 各部位电桥数据到达回调
    std::function<void(uint8_t part_id, uint16_t positive, uint16_t negative)> onpart_bridge_arrived =
        [](auto&&...) {};

private:
    uint8_t                                    active_part_ = 0x01; // 当前活动的从机地址
    std::map<uint8_t, touch::matrix<uint16_t>> latest_frames_;      // 各部位最新帧
    std::map<uint8_t, uint32_t>                last_frame_tick_;    // 各部位最后接收时间
    std::map<uint8_t, bool>                    parts_discovered_;   // 已发现的部位

    static constexpr uint32_t FRAME_TIMEOUT_MS = 1000;
};

#endif // MULTI_PART_DEVICE_H
