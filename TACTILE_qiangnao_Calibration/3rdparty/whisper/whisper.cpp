#include "whisper.h"

#include <algorithm>
#include <chrono>

#ifdef ZYNQ
#include "sleep.h"
#endif

using namespace std::chrono_literals;

// [0:1]
#define METHOD_PUT      0x00
#define METHOD_GET      0x01
#define METHOD_RESPONSE 0x02

namespace whisper
{
    whisper::whisper(const std::shared_ptr<packager>& pkger)
    {
        pkger_          = pkger;
        pkger_->onready = [=, this](const uint8_t channel, const uint8_t flags, payload& data) {
            if (flags & METHOD_RESPONSE) {
#if !defined(ZYNQ)
                std::unique_lock lock(mtx_);
#endif
                response_[channel] = { static_cast<uint8_t>(flags >> 2), data };
#if !defined(ZYNQ)
                cv_.notify_all();
#endif
            }
            else if (handlers_.contains(channel)) {
                // Streaming channels must not answer a corrupted GET flag as an empty response.
                if (put_only_channels_.contains(channel) && (flags & 0x03U) != METHOD_PUT) return;
                const auto response_data = handlers_[channel](data);
                if (flags & METHOD_GET) {
                    response(channel, flags & 0xFC, response_data);
                }
                else if (response_data.size() > 0) {
                    put(channel, response_data);
                }
            }
        };

        pkger_->onerror = [this](const auto& msg) { onerror(msg); };
    }

    void whisper::put(const uint8_t channel, const payload& data)
    {
        const auto pkg = pkger_->pack(channel, METHOD_PUT, data);
        tx(pkg.data(), pkg.size());
    }

    void whisper::get(uint8_t channel, const payload& data,
                      const std::function<void(payload&)>& handler,
                      const std::function<void(const std::string&)>& fail)
    {
        get(channel, data, handler, fail, 200ms);
    }

    void whisper::get(uint8_t channel, const payload& data, const std::function<void(payload&)>& handler,
                      const std::function<void(const std::string&)>& fail,
                      std::chrono::milliseconds timeout)
    {
        uint8_t pkg_id;
        std::vector<uint8_t> pkg;
        {
            std::unique_lock lock(mtx_);

            cnt_[channel]  = cnt_.contains(channel) ? cnt_[channel] + 1 : 0;
            pkg_id = cnt_[channel] % 64;

            pkg = pkger_->pack(channel, (pkg_id << 2) | METHOD_GET, data);
            // 标定 V1（channel 7）的业务头自带 sequence/session。早期五指固件
            // 回复时未回显 Whisper package-id，固定使用 RESPONSE(0x02)。先清除
            // 旧回复，再允许 channel 7 接受任意外层 package-id；随后仍由上层
            // decode_calibration_response() 严格校验 command/part/sequence/session。
            response_.erase(channel);
            tx(pkg.data(), pkg.size());
            const auto deadline = std::chrono::steady_clock::now() + timeout;
            const auto response_ready = [=, this]() {
                return response_.contains(channel) &&
                       ((channel == 0x07U) || (response_[channel].first == pkg_id));
            };
            while (!response_ready()) {
                const auto now = std::chrono::steady_clock::now();
                if (now >= deadline) {
                    fail("request #" + std::to_string(pkg_id) + " timeout");
                    return;
                }
                const auto wait_time = std::min(std::chrono::milliseconds(300),
                                                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now));
                if (cv_.wait_for(lock, wait_time, response_ready)) break;
                tx(pkg.data(), pkg.size());
            }
        }

        handler(response_[channel].second);
    }

    void whisper::response(const uint8_t channel, const uint8_t flags, const payload& data)
    {
        const auto pkg = pkger_->pack(channel, flags | METHOD_RESPONSE, data);
        tx(pkg.data(), pkg.size());
    }

    void whisper::on(const uint8_t channel, const std::function<payload(payload&)>& handler,
                     const bool put_only)
    {
        handlers_[channel] = handler;
        if (put_only) put_only_channels_.insert(channel);
        else put_only_channels_.erase(channel);
    }

    // physical layer
    void whisper::rx(const void *buffer, const size_t size) { pkger_->unpack(buffer, size); }

} // namespace whisper
