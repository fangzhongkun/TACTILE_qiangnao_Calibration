// 修改 Device 基类，支持部位ID回调

struct Device
{
    // 原有回调（保持兼容）
    std::function<void(uint32_t id, const touch::matrix<uint16_t>&)> onarrived;

    // 【新增】多部位数据回调
    std::function<void(uint8_t part_id, uint32_t sensor_id, const touch::matrix<uint16_t>&)>
        onpart_data_arrived = [](auto&&...) {};

    // 【新增】解析带部位ID的数据帧
    template<class FMT>
    std::pair<uint8_t, touch::matrix<FMT>> parse_frame_with_part(whisper::payload& payload)
    {
        // 读取部位ID（第一个字节）
        const auto part_id = payload.read<uint8_t>();

        // 读取矩阵维度和数据
        const auto cols = payload.read<uint8_t>();
        const auto rows = payload.read<uint8_t>();
        const auto size = rows * cols * sizeof(FMT);

        if (!size || size > payload.remain_size()) {
            return { part_id, {} };
        }

        return { part_id, touch::matrix<FMT>(rows, cols, payload.read(size)) };
    }
};
