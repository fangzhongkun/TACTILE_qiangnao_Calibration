#include "config.h"

#include <QFile>
#include <QIODeviceBase>
#include <QStandardPaths>
#include <QTextStream>

#define JSON_GET(V, J, KEY)                                                                                \
    if ((J).contains(KEY)) V = (J)[KEY].get<decltype(V)>()

namespace config
{
    void from_json(const json& j)
    {
        JSON_GET(language, j, "language");
        JSON_GET(theme, j, "theme");

        if (j.contains("layout")) {
            if (j["layout"].contains("visibility")) {
                using namespace layout::visibility;

                JSON_GET(toolbar, j["layout"]["visibility"], "toolbar");
                JSON_GET(statusbar, j["layout"]["visibility"], "statusbar");
                JSON_GET(center_window, j["layout"]["visibility"], "center-window");
                JSON_GET(statistics_window, j["layout"]["visibility"], "statistics-window");
                JSON_GET(values_window, j["layout"]["visibility"], "values-window");
                JSON_GET(log_window, j["layout"]["visibility"], "logging-window");
            }
        }

        if (j.contains("record")) {
            if (j["record"].contains("image")) {
                using namespace record::image;

                JSON_GET(path, j["record"]["image"], "save-path");
            }

            if (j["record"].contains("csv")) {
                using namespace record::csv;

                JSON_GET(path, j["record"]["csv"], "save-path");
            }
        }
    }

    json to_json()
    {
        json j;

        j["language"] = language;
        j["theme"]    = theme;

        j["layout"]["visibility"]["toolbar"]           = layout::visibility::toolbar;
        j["layout"]["visibility"]["statusbar"]         = layout::visibility::statusbar;
        j["layout"]["visibility"]["center-window"]     = layout::visibility::center_window;
        j["layout"]["visibility"]["statistics-window"] = layout::visibility::statistics_window;
        j["layout"]["visibility"]["values-window"]     = layout::visibility::values_window;
        j["layout"]["visibility"]["logging-window"]    = layout::visibility::log_window;

        j["record"]["image"]["save-path"] = record::image::path;
        j["record"]["csv"]["save-path"]   = record::csv::path;

        return j;
    }
} // namespace config

namespace config
{
    void load()
    {
        configpath =
            QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/" + filename;
        // load configure file
        QString text;
        QFile   file(configpath);
        if (file.open(QIODevice::ReadWrite | QIODevice::Text)) {
            QTextStream in(&file);
            text = in.readAll();
        }

        // parse json string
        try {
            from_json(json::parse(text.toStdString()));
        }
        catch (json::parse_error&) {
        }

        if (record::image::path.isEmpty())
            record::image::path = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);

        if (record::csv::path.isEmpty())
            record::csv::path = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    }

    void save()
    {
        QFile file(configpath);

        if (!file.open(QIODevice::ReadWrite | QIODevice::Truncate | QIODevice::Text)) return;

        QTextStream out(&file);
        out << to_json().dump(4).c_str();

        file.close();
    }
} // namespace config
