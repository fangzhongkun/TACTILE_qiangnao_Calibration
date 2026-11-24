#ifndef SCANNER_CONFIG_H
#define SCANNER_CONFIG_H

#include "json.h"

#include <string>

namespace config
{
    inline std::string theme{ "dark" };
    inline QString     language{ "zh_CN" };
    inline QString     configpath{};

    namespace layout::visibility
    {
        inline bool toolbar{ true };
        inline bool statusbar{ true };
        inline bool center_window{ true };
        inline bool statistics_window{ true };
        inline bool log_window{ false };
        inline bool values_window{ true };
        inline bool serial_rx_window{ false };
        inline bool serial_tx_window{ false };
    } // namespace layout::visibility

    namespace record
    {
        namespace image
        {
            inline QString path{};
        }

        namespace csv
        {
            inline QString path{};
        }
    } // namespace record

    void from_json(const json& j);
    json to_json();
} // namespace config

namespace config
{
    inline constexpr auto filename = "scanner_pt.json";

    void load();
    void save();
} // namespace config

#endif //! SCANNER_CONFIG_H