#ifndef LIBTOUCH_GESTURE_HPP
#define LIBTOUCH_GESTURE_HPP

#include "processing.h"

#include <algorithm>

namespace touch
{
    template<typename T> bool gesture_tracker<T>::is_moved()
    {
        if (touches_.size() > MOVE_MAX_TOUCHES_SIZE || tracks_.size() > MOVE_MAX_TOUCHES_SIZE) return false;

        for (const auto& touch : touches_) {
            if (touch.area > GRASP_AREA_THRESHOLD / 2) {
                return false;
            }
        }

        for (auto& track : tracks_) {
            const auto dis = distance(track[0], track[1]);

            // too fast
            if (dis > 6) {
                track[1] = track[0];
                return false;
            }

            // accumulate to
            if (dis >= MOVE_DIS_THRESHOLD) return true;
        }

        return false;
    }

    template<typename T> bool gesture_tracker<T>::is_grasp()
    {
        size_t max_area = 0;
        size_t max_peak = 0;
        size_t sum_area = 0;

        for (const auto& touch : touches_) {
            max_area = (max_area < touch.area) ? touch.area : max_area;
            max_peak = (max_peak < touch.value) ? touch.value : max_peak;
            sum_area += touch.area;
        }

        return (sum_area > GRASP_AREA_THRESHOLD) && (max_peak > GRASP_PEAK_THRESHOLD) &&
               (max_area > (GRASP_AREA_THRESHOLD / 2));
    }

    template<typename T> void gesture_tracker<T>::update_tracks()
    {
        // remove some touches
        {
            if (touches_.size() > MOVE_MAX_TOUCHES_SIZE) {
                touches_.erase(std::remove_if(touches_.begin(), touches_.end(),
                                              [](const auto& x) { return x.area <= 3; }),
                               touches_.end());
            }

            if (touches_.size() > MOVE_MAX_TOUCHES_SIZE) {
                touches_.erase(touches_.begin() + MOVE_MAX_TOUCHES_SIZE, touches_.end());
            }
        }

        double dis = 0;

        // empty
        if (tracks_.empty()) {
            for (const auto& touch : touches_) {
                tracks_.push_back({ touch.center(), touch.center() });
            }
            return;
        }

        // normal
        if (tracks_.size() == touches_.size()) {
            for (size_t i = 0; i < touches_.size(); ++i) {
                auto d = distance(touches_[i].center(), tracks_[i][1]);
                dis    = (dis < d) ? d : dis;

                tracks_[i][1] = touches_[i].center();
            }
        }
        // +/- fingers
        else {
            std::vector<point<int>> t1(tracks_.size());  // for old tracks
            std::vector<point<int>> t2(touches_.size()); // for new tracks

            for (size_t i = 0; i < tracks_.size(); ++i) t1[i] = tracks_[i][1];

            for (size_t i = 0; i < touches_.size(); ++i) t2[i] = touches_[i].center();

            // match old & new tracks
            const auto indices = mapping(t1, t2);

            // - fingers
            if (tracks_.size() > touches_.size()) {
                for (size_t i = 0; i < indices.size(); ++i) {
                    auto d = distance(touches_[i].center(), tracks_[indices[i]][1]);
                    dis    = (dis < d) ? d : dis;

                    tracks_[indices[i]][1] = touches_[i].center();
                }
            }
            // + fingers
            else {
                std::vector<std::array<point<int>, 2>> tracks{};
                // all
                for (size_t i = 0; i < touches_.size(); ++i) {
                    tracks.push_back({ touches_[i].center(), touches_[i].center() });
                }

                // copy the start position of old fingers
                for (size_t i = 0; i < indices.size(); ++i) {
                    auto d = distance(tracks[indices[i]][1], tracks_[i][1]);
                    dis    = (dis < d) ? d : dis;

                    tracks[indices[i]][0] = tracks_[i][0];
                }

                tracks_ = tracks;
            }
        }

        const auto t = (clock::us() - speed_.second).count() / 1000000.0;
        speed_       = { speed_.first * 0.85 + 0.15 * (dis / t), clock::us() };
    }

    template<typename T> void gesture_tracker<T>::set_gesture(const gesture_t value)
    {
        if (gesture_ == value) return;

        // DOUBLE_CLICK
        if (gesture_ == gesture_t::PRESS && value == gesture_t::RELEASE) {
            last_press_released_ts_ = clock::us();
        }
        else if (value != gesture_t::NONE) {
            last_press_released_ts_ = 0ms;
        }

        //
        gesture_ = value;

        if (gesture_ != gesture_t::PRESS) ts_ = clock::us();

        // MOVE
        switch (value) {
        case gesture_t::MOVE: break;

        default:
            tracks_ = {};
            speed_  = {};
            break;
        }
    }

    template<typename T> gesture<T> gesture_tracker<T>::track(const std::vector<touch<T>>& touches)
    {
        // remove isolated point
        touches_ = {};
        for (const auto& touch : touches)
            if (touch.area >= POINT_VALID_SIZE) touches_.push_back(touch);

        const size_t N = touches_.size();

        if (N > MAX_TOUCHES_SIZE) set_gesture(gesture_t::INVALID);

        switch (gesture_) {
        case gesture_t::INVALID:
            if (N <= MAX_TOUCHES_SIZE) set_gesture(gesture_t::NONE);
            break;

        case gesture_t::NONE:
            if (N <= 0) break;

            // grasp & press
            if (enabled_[gesture_t::GRASP] && is_grasp()) {
                set_gesture(gesture_t::GRASP);
                break;
            }

            set_gesture((enabled_[gesture_t::DOUBLE_CLICK] &&
                         (clock::us() - last_press_released_ts_ < 200ms) &&
                         ((clock::us() - last_press_released_ts_ > 50ms)))
                            ? gesture_t::DOUBLE_CLICK
                            : gesture_t::PRESS);
            break;

        case gesture_t::PRESS:
            if (N <= 0) {
                set_gesture(gesture_t::RELEASE);
                break;
            }

            // 1. GRASP
            if (enabled_[gesture_t::GRASP] && is_grasp()) {
                set_gesture(gesture_t::GRASP);
                break;
            }

            //
            {
                update_tracks();

                // 2. MOVE
                if (enabled_[gesture_t::MOVE] && is_moved()) {
                    set_gesture(gesture_t::MOVE);
                    break;
                }

                // 3. LONGPRESS
                if (enabled_[gesture_t::LONGPRESS] && (tracks_.size() == 1) && (clock::us() - ts_ > 1s)) {
                    set_gesture(gesture_t::LONGPRESS);
                    break;
                }
            }

            break;

        case gesture_t::LONGPRESS:
            if (N <= 0) {
                set_gesture(gesture_t::RELEASE);
                break;
            }

            // MOVE
            {
                update_tracks();
                if (enabled_[gesture_t::MOVE] && is_moved()) {
                    set_gesture(gesture_t::MOVE);
                    break;
                }
            }
            break;

        case gesture_t::DOUBLE_CLICK:
        case gesture_t::GRASP:
            if (N <= 0) set_gesture(gesture_t::RELEASE);
            break;

        case gesture_t::RELEASE: set_gesture(gesture_t::NONE); break;

        case gesture_t::MOVE:
            xts_ = (N <= 0 && xts_ == 0ms) ? clock::us() : 0ms;

            if (N > 0) update_tracks();

            //
            if (N == 0 && (clock::us() - xts_ > 100ms)) {
                set_gesture(gesture_t::RELEASE);
            }
            break;

        default: break;
        }

        return { gesture_, touches_, tracks_, speed_.first, clock::us() - ts_ };
    }
} // namespace touch

#endif //! LIBTOUCH_GESTURE_HPP
