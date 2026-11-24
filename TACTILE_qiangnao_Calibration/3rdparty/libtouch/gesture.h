#ifndef LIBTOUCH_GESTURE_H
#define LIBTOUCH_GESTURE_H

#include "clock.h"
#include "types.h"

#include <array>
#include <map>
#include <string>
#include <vector>

// TODO: conflict with <Windows.h>
#ifdef DOUBLE_CLICK
#undef DOUBLE_CLICK
#endif

namespace touch
{
    enum class gesture_t : uint8_t
    {
        INVALID      = 0x00,
        NONE         = 0x01,
        //
        PRESS        = 0x02,
        LONGPRESS    = 0x03,
        RELEASE      = 0x04,
        //
        GRASP        = 0x05,
        //
        MOVE         = 0x06,
        //
        DOUBLE_CLICK = 0x08,
    };

    std::string to_string(gesture_t);

    template<typename T> struct gesture
    {
        gesture_t type{ gesture_t::NONE };

        std::vector<touch<T>> touches{};

        std::vector<std::array<point<int>, 2>> tracks{};

        double speed{};

        std::chrono::microseconds duration{};

        // total area
        T total_area() const
        {
            T value = 0;
            for (const auto& touch : touches) {
                value += static_cast<uint16_t>(touch.area);
            }
            return value;
        }

        // peak value of all touches
        T peak() const
        {
            T value = 0;
            for (const auto& touch : touches) {
                if (value < static_cast<T>(touch.value)) {
                    value = static_cast<T>(touch.value);
                }
            }
            return value;
        }

        // start & end position of the move gesture
        [[nodiscard]] std::pair<point<uint16_t>, point<uint16_t>> track() const
        {
            int             d = 0;
            point<uint16_t> s{}, e{};

            for (const auto& t : tracks) {
                if (d < distance(t[1], t[0])) {
                    d = static_cast<int>(distance(t[1], t[0]));
                    s = { static_cast<uint16_t>(t[0].x), static_cast<uint16_t>(t[0].y) };
                    e = { static_cast<uint16_t>(t[1].x), static_cast<uint16_t>(t[1].y) };
                }
            }

            return { s, e };
        }
    };

    //

#define MAX_TOUCHES_SIZE      16
#define MOVE_MAX_TOUCHES_SIZE 6

    template<typename T> class gesture_tracker
    {
    public:
        gesture_tracker() = default;

        gesture<T> track(const std::vector<touch<T>>& touches);

        size_t set_grasp_area_threshold(const size_t value)
        {
            GRASP_AREA_THRESHOLD = std::max<size_t>(value, 65);
            return GRASP_AREA_THRESHOLD;
        }

        size_t set_grasp_peak_threshold(const size_t value)
        {
            GRASP_PEAK_THRESHOLD = value;
            return GRASP_PEAK_THRESHOLD;
        }

        void enable(gesture_t g, bool en) { enabled_[g] = en; }

        void set_move_distance_threshold(double d) { MOVE_DIS_THRESHOLD = d; }

        void set_point_valid_size(int s) { POINT_VALID_SIZE = s; }

        void clear() { set_gesture(gesture_t::RELEASE); }

    private:
        bool is_moved();
        bool is_grasp();

        void update_tracks();

        void set_gesture(gesture_t value);

    private:
        size_t GRASP_AREA_THRESHOLD = 100;
        size_t GRASP_PEAK_THRESHOLD = 0;
        double MOVE_DIS_THRESHOLD   = 4;
        size_t POINT_VALID_SIZE     = 2;

        std::vector<touch<T>>                        touches_{};
        std::vector<std::array<point<int>, 2>>       tracks_{}; // MOVE
        std::pair<double, std::chrono::microseconds> speed_{};  // MOVE
        std::chrono::microseconds                    xts_{};    // MOVE

        gesture_t                 gesture_{ gesture_t::NONE };
        std::map<gesture_t, bool> enabled_{
            { gesture_t::LONGPRESS, true },
            { gesture_t::GRASP, true },
            { gesture_t::MOVE, true },
            { gesture_t::DOUBLE_CLICK, true },
        };
        std::chrono::microseconds ts_{};                     // timestamp

        std::chrono::microseconds last_press_released_ts_{}; // DOUBLE_CLICK
    };

} // namespace touch

#include "gesture.hpp"

#endif //! LIBTOUCH_GESTURE_H
