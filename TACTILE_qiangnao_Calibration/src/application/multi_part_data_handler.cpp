#include "multi_part_data_handler.h"

#include "logging.h"

#include <chrono>

namespace app
{

    MultiPartDataHandler::MultiPartDataHandler() { logi("MultiPartDataHandler initialized"); }

    void MultiPartDataHandler::handlePartFrame(uint8_t part_id, const touch::matrix<uint16_t>& matrix)
    {
        if (matrix.empty()) {
            return;
        }

        std::lock_guard<std::mutex> lock(mutex_);

        PartDataPacket packet;
        packet.part_id         = part_id;
        packet.timestamp       = getCurrentTick();
        packet.matrix          = matrix;
        packet.has_bridge_data = false;

        // 检查是否有缓存的电桥数据需要合并
        auto it = latest_data_.find(part_id);
        if (it != latest_data_.end() && it->second.has_bridge_data) {
            // 如果时间戳接近（小于100ms），认为是同一帧的数据
            if ((packet.timestamp - it->second.timestamp) < 100) {
                packet.bridge_positive = it->second.bridge_positive;
                packet.bridge_negative = it->second.bridge_negative;
                packet.has_bridge_data = true;
            }
        }

        latest_data_[part_id] = packet;
        frame_counters_[part_id]++;

        // 触发回调
        if (on_part_data_) {
            on_part_data_(packet);
        }

        // 日志记录（每100帧打印一次，避免刷屏）
        if (frame_counters_[part_id] % 100 == 0) {
            logi("Part 0x{:02X}: received {} frames, latest size {}x{}", part_id, frame_counters_[part_id],
                 matrix.rows, matrix.cols);
        }
    }

    void MultiPartDataHandler::handlePartBridge(uint8_t part_id, uint16_t positive, uint16_t negative)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = latest_data_.find(part_id);
        if (it != latest_data_.end()) {
            // 更新最新帧的电桥数据
            it->second.bridge_positive = positive;
            it->second.bridge_negative = negative;
            it->second.has_bridge_data = true;
        }
        else {
            // 暂存电桥数据，等待矩阵数据到达
            PartDataPacket packet;
            packet.part_id         = part_id;
            packet.timestamp       = getCurrentTick();
            packet.bridge_positive = positive;
            packet.bridge_negative = negative;
            packet.has_bridge_data = true;
            latest_data_[part_id]  = packet;
        }
    }

    PartDataPacket MultiPartDataHandler::getLatestData(uint8_t part_id) const
    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = latest_data_.find(part_id);
        if (it != latest_data_.end()) {
            return it->second;
        }

        return PartDataPacket{}; // 返回空包
    }

    std::map<uint8_t, PartDataPacket> MultiPartDataHandler::getAllLatestData() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return latest_data_;
    }

    bool MultiPartDataHandler::isPartOnline(uint8_t part_id, uint32_t timeout_ms) const
    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = latest_data_.find(part_id);
        if (it == latest_data_.end()) {
            return false;
        }

        uint32_t now     = getCurrentTick();
        uint32_t elapsed = now - it->second.timestamp;

        return elapsed < timeout_ms;
    }

    void MultiPartDataHandler::setOnPartDataCallback(std::function<void(const PartDataPacket&)> callback)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        on_part_data_ = std::move(callback);
    }

    void MultiPartDataHandler::clearAll()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        latest_data_.clear();
        frame_counters_.clear();
        logi("All part data cleared");
    }

    MultiPartDataHandler::Statistics MultiPartDataHandler::getStatistics() const
    {
        std::lock_guard<std::mutex> lock(mutex_);

        Statistics stats;
        stats.frame_count = frame_counters_;

        for (const auto& [part_id, packet] : latest_data_) {
            stats.last_tick[part_id] = packet.timestamp;
        }

        return stats;
    }

    uint32_t MultiPartDataHandler::getCurrentTick() const
    {
        using namespace std::chrono;
        auto now = steady_clock::now();
        auto ms  = duration_cast<milliseconds>(now.time_since_epoch());
        return static_cast<uint32_t>(ms.count());
    }

} // namespace app
