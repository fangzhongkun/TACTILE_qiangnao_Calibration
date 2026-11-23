/**
 * @file multi_part_data_handler.h
 * @brief 多部位数据接收处理器
 *
 * 解决上位机如何区分来自不同手指的数据问题
 */

#ifndef MULTI_PART_DATA_HANDLER_H
#define MULTI_PART_DATA_HANDLER_H

#include "libtouch/matrix.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>

namespace app
{

    /**
     * @brief 部位数据包结构
     */
    struct PartDataPacket
    {
        uint8_t                 part_id;         // 部位ID (0x01-0x05)
        uint32_t                timestamp;       // 时间戳
        touch::matrix<uint16_t> matrix;          // 传感器矩阵
        uint16_t                bridge_positive; // 电桥正值（可选）
        uint16_t                bridge_negative; // 电桥负值（可选）
        bool                    has_bridge_data; // 是否包含电桥数据
    };

    /**
     * @brief 多部位数据处理器
     *
     * 功能：
     * 1. 从混合数据流中分离各部位数据
     * 2. 缓存最新数据帧
     * 3. 提供回调机制
     */
    class MultiPartDataHandler
    {
    public:
        MultiPartDataHandler();
        ~MultiPartDataHandler() = default;

        /**
         * @brief 处理接收到的数据帧
         * @param part_id 部位ID
         * @param matrix 传感器矩阵
         */
        void handlePartFrame(uint8_t part_id, const touch::matrix<uint16_t>& matrix);

        /**
         * @brief 处理电桥数据
         * @param part_id 部位ID
         * @param positive 正值
         * @param negative 负值
         */
        void handlePartBridge(uint8_t part_id, uint16_t positive, uint16_t negative);

        /**
         * @brief 获取指定部位的最新数据
         * @param part_id 部位ID
         * @return 数据包（如果没有则matrix为空）
         */
        PartDataPacket getLatestData(uint8_t part_id) const;

        /**
         * @brief 获取所有部位的最新数据
         * @return 部位ID到数据包的映射
         */
        std::map<uint8_t, PartDataPacket> getAllLatestData() const;

        /**
         * @brief 检查部位是否在线
         * @param part_id 部位ID
         * @param timeout_ms 超时时间（毫秒）
         * @return 是否在线
         */
        bool isPartOnline(uint8_t part_id, uint32_t timeout_ms = 1000) const;

        /**
         * @brief 设置数据到达回调
         */
        void setOnPartDataCallback(std::function<void(const PartDataPacket&)> callback);

        /**
         * @brief 清除所有缓存数据
         */
        void clearAll();

        /**
         * @brief 获取统计信息
         */
        struct Statistics
        {
            std::map<uint8_t, uint64_t> frame_count; // 各部位接收帧数
            std::map<uint8_t, uint64_t> drop_count;  // 各部位丢帧数
            std::map<uint8_t, uint32_t> last_tick;   // 各部位最后接收时间
        };
        Statistics getStatistics() const;

    private:
        uint32_t getCurrentTick() const;

        mutable std::mutex                         mutex_;
        std::map<uint8_t, PartDataPacket>          latest_data_;
        std::map<uint8_t, uint64_t>                frame_counters_;
        std::function<void(const PartDataPacket&)> on_part_data_;
    };

} // namespace app

#endif // MULTI_PART_DATA_HANDLER_H
