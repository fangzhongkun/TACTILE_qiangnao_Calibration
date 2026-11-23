#include "dhs-master-uint16-device.h"

#include "logging.h"

#include <chrono>
#include <thread>

constexpr uint8_t CHANNEL_METAINFO = 0x01;
constexpr uint8_t CHANNEL_FRAMES   = 0x02;
constexpr uint8_t CHANNEL_LOGGING  = 0x05;
constexpr uint8_t CHANNEL_CONTROL  = 0x06;
constexpr uint8_t CHANNEL_ZEROING  = 0x07;


DhsMasterUint16Device::~DhsMasterUint16Device()
{
    running_ = false;
    wait_ = true; // 解除暂停等待，保证 jthread 析构时可以正常退出。
}

int DhsMasterUint16Device::open(int dev)
{
    prepare();
    // 0xff 表示尚未发现从机，避免扫描失败时误把 0x00 当成有效从机。
    slave_            = 0xff;
    whisper_->onerror = [](const auto& err) { loge("{}", err); };

    if (serial_->open(options_) != 0) {
        loge("failed to open the device");
        return -1;
    }
    thread_ = std::jthread([=, this]() {
        probe::thread::set_name("MASTER-DATA-CH");

        // scan devices
        bool found = false;
        for (uint8_t i = 0; i < 16 && !found; i++) {
            whisper_->get(
                (i << 4) | CHANNEL_METAINFO, whisper::payload{ 0x01 },
                [&](whisper::payload& payload) {
                    slave_ = i;
                    found  = true;
                },
                [=](const std::string& err) { loge("failed to connect the {:#04x} slave: {}", i, err); });
        }
        logi_if(found, "found slave: {:#04x}", slave_);
        logw_if(!found, "no slave found on master uint16 device");

        running_              = found;
        constexpr auto PERIOD = std::chrono::milliseconds(100); // 10Hz
        while (running_) {
            auto loop_start = std::chrono::steady_clock::now();
            while (running_ && !wait_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            if (!running_) {
                break;
            }
            whisper_->get((slave_ << 4) | CHANNEL_FRAMES, { 0x01 },
                          [=, this](whisper::payload& payload) -> whisper::payload {
                              /*
                               * OP3 的 MCU payload 不是“最前面只有一个总包数字段”，而是每个逻辑包
                               * 都带 4 字节包头：
                               *   [total_package][current_package][rows][cols][data...]
                               *
                               * 例如 AD 模式双逻辑包为：
                               *   02 01 0A 08 [160 bytes array]
                               *   02 02 01 02 [positive u16][negative u16]
                               *
                               * 上一版代码只在 payload 开头读取了一次 total_package，导致第 2 包的
                               * total_package=0x02 被误当成 current_package，随后 parse_frame() 又把
                               * current_package=0x02 当成维度字段，最终把包头字节 0x02 拼进了 u16 数据，
                               * 出现 770/1026 这类错误值。
                               */
                              bool has_touch_frame = false;
                              touch::matrix<uint16_t> touch_frame{};

                              uint8_t expected_total_packages = 0U;
                              uint8_t parsed_packages = 0U;

                              while (payload.remain_size() > 0U) {
                                  if (payload.remain_size() < 4U) {
                                      loge("failed to parse OP3 frame: logical package header is incomplete, parsed={}, remain={}",
                                           parsed_packages, payload.remain_size());
                                      return {};
                                  }

                                  const auto total_packages   = payload.read<uint8_t>();
                                  const auto current_package = payload.read<uint8_t>();

                                  if (total_packages == 0U) {
                                      loge("failed to parse OP3 frame: package count is 0");
                                      return {};
                                  }

                                  if (expected_total_packages == 0U) {
                                      expected_total_packages = total_packages;
                                  }
                                  else if (total_packages != expected_total_packages) {
                                      logw("OP3 logical package total count changed inside one frame: expected={}, current={}",
                                           expected_total_packages, total_packages);
                                  }

                                  auto frame = parse_frame<uint16_t>(payload);
                                  if (frame.empty()) {
                                      loge("failed to parse OP3 frame: current_package={}, total={}, remain={}",
                                           current_package, total_packages, payload.remain_size());
                                      return {};
                                  }

                                  ++parsed_packages;

                                  if (current_package == 0x01U) {
                                      // 第 1 个逻辑包：完整 8×10 阵列数据，用于正常显示和质心计算。
                                      touch_frame = frame;
                                      has_touch_frame = true;
                                  }
                                  else if (current_package == 0x02U) {
                                      // 第 2 个逻辑包：AW86872 差分电桥数据，数据区为两个 uint16_t：正值、负值。
                                      if (frame.size() >= 2) {
                                          const auto *data = frame.ptr();
                                          onbridgearrived(data[0], data[1]);
                                      }
                                      else {
                                          loge("failed to parse OP3 bridge packet: size={} < 2", frame.size());
                                      }
                                  }
                                  else {
                                      logw("unknown OP3 logical package ignored: current_package={}, total={}",
                                           current_package, total_packages);
                                  }

                                  if (parsed_packages >= expected_total_packages) {
                                      break;
                                  }
                              }

                              if (parsed_packages != expected_total_packages) {
                                  logw("OP3 logical package count mismatch: parsed={}, expected={}",
                                       parsed_packages, expected_total_packages);
                              }

                              if (has_touch_frame) {
                                  onarrived(0x00, touch_frame);
                              }

                              return {};
                          },
                          [=, this](const std::string& err) {
                              loge("failed to query data form {:#04x} slave: {}", slave_, err);
                          });
            auto elapsed = std::chrono::steady_clock::now() - loop_start;
            if (elapsed < PERIOD) {
                std::this_thread::sleep_for(PERIOD - elapsed);
            }
        }
    });

    ///
    return serial_->ready();
}
