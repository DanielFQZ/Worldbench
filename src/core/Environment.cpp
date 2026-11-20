#include "Environment.h"
#include <QRegularExpression>
#include <QtMath>
#include <cmath>

namespace wb {
QVector3D Environment::sunDirection() const {
    const auto a = qDegreesToRadians(double(azimuth)), e = qDegreesToRadians(double(elevation));
    return {float(std::sin(a) * std::cos(e)), float(std::sin(e)), float(std::cos(a) * std::cos(e))};
}
QJsonObject saveEnvironment(const Environment &e) {
    return {{"enabled", e.enabled},
            {"sky", e.sky},
            {"sun", e.sun},
            {"atmosphere", e.atmosphere},
            {"skyIntensity", e.skyIntensity},
            {"sunIntensity", e.sunIntensity},
            {"azimuth", e.azimuth},
            {"elevation", e.elevation},
            {"haze", e.haze},
            {"exposure", e.exposure},
            {"skyColor", e.skyColor},
            {"horizonColor", e.horizonColor},
            {"groundColor", e.groundColor},
            {"sunColor", e.sunColor}};
}
bool loadEnvironment(const QJsonObject &o, Environment &e, QString &error) {
    Environment next;
    auto fail = [&] {
        error = "环境参数缺失、类型错误或越界";
        return false;
    };
    const auto keys = saveEnvironment(next).keys();
    for (auto it = o.begin(); it != o.end(); ++it)
        if (!keys.contains(it.key()))
            return fail();
    for (auto [name, target] : {std::pair{"enabled", &next.enabled},
                                {"sky", &next.sky},
                                {"sun", &next.sun},
                                {"atmosphere", &next.atmosphere}}) {
        if (!o[name].isBool())
            return fail();
        *target = o[name].toBool();
    }
    auto number = [&](const char *name, int &target, int lo, int hi) {
        auto v = o[name];
        double n = v.toDouble();
        if (!v.isDouble() || !std::isfinite(n) || n != std::floor(n) || n < lo || n > hi)
            return false;
        target = int(n);
        return true;
    };
    if (!number("skyIntensity", next.skyIntensity, 0, 200) ||
        !number("sunIntensity", next.sunIntensity, 0, 300) || !number("azimuth", next.azimuth, -180, 180) ||
        !number("elevation", next.elevation, 0, 90) || !number("haze", next.haze, 0, 100) ||
        !number("exposure", next.exposure, -30, 30))
        return fail();
    static const QRegularExpression color("^#[0-9a-fA-F]{6}$");
    for (auto [name, target] : {std::pair{"skyColor", &next.skyColor},
                                {"horizonColor", &next.horizonColor},
                                {"groundColor", &next.groundColor},
                                {"sunColor", &next.sunColor}}) {
        if (!o[name].isString() || !color.match(o[name].toString()).hasMatch())
            return fail();
        *target = o[name].toString().toLower();
    }
    e = next;
    return true;
}
bool patchEnvironment(const Environment &before, const QJsonObject &changes, Environment &after,
                      QString &error) {
    auto o = saveEnvironment(before);
    for (auto it = changes.begin(); it != changes.end(); ++it)
        o[it.key()] = it.value();
    return loadEnvironment(o, after, error);
}
} // namespace wb
