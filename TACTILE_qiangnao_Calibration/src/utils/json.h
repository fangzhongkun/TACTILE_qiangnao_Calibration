#ifndef SCANNER_JSON_H
#define SCANNER_JSON_H

#include <nlohmann/json.hpp>
#include <QColor>
#include <QKeySequence>
#include <QPoint>

using json = nlohmann::json;

// clang-format off
inline void from_json(const json& j, QString& qstr)         { qstr = QString::fromStdString(j.get<std::string>()); }
inline void to_json(json& j, const QString& qstr)           { j = qstr.toStdString(); }

inline void from_json(const json& j, QColor& c)             { c = { j.get<QString>() }; }
inline void to_json(json& j, const QColor& c)               { j = c.name(QColor::HexArgb); }

inline void from_json(const json& j, QKeySequence& key)     { key = { j.get<QString>() }; }
inline void to_json(json& j, const QKeySequence& key)       { j = key.toString(); }

inline void from_json(const json& j, Qt::PenStyle& style)   { style = static_cast<Qt::PenStyle>(j.get<int>()); }
inline void to_json(json& j, const Qt::PenStyle& style)     { j = style; }

inline void from_json(const json& j, QPoint& p)             { p = QPoint(j["num"].get<int>(), j["den"].get<int>()); }
inline void to_json(json& j, const QPoint& p)               { j["num"] = p.x(); j["den"] = p.y(); }
// clang-format on

#endif //! SCANNER_JSON_H
