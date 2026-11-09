#pragma once
#include <QColor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <array>
#include <optional>

inline std::optional<std::array<QColor, 4>> decodeRibbonFrame(const QString &json)
{
    const auto document = QJsonDocument::fromJson(json.toUtf8());
    if (!document.isObject()) return std::nullopt;
    const auto object = document.object();
    if (object.value("version").toInt() != 1 || !object.value("active").isBool()) return std::nullopt;
    const auto zones = object.value("rgb").toArray();
    if (zones.size() != 4) return std::nullopt;
    std::array<QColor, 4> result;
    for (int zone = 0; zone < 4; ++zone) {
        const auto rgb = zones[zone].toArray();
        if (rgb.size() != 3) return std::nullopt;
        std::array<int, 3> values;
        for (int channel = 0; channel < 3; ++channel) {
            const double value = rgb[channel].toDouble(-1);
            if (value < 0 || value > 255 || value != int(value)) return std::nullopt;
            values[channel] = int(value);
        }
        result[zone] = object.value("active").toBool()
            ? QColor(values[0], values[1], values[2]) : QColor(Qt::black);
    }
    return result;
}
