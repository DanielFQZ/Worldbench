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
            {"clouds", e.clouds},
            {"water", e.water},
            {"waterWaves", e.waterWaves},
            {"waterReflections", e.waterReflections},
            {"shadows", e.shadows},
            {"waveStrength", e.waveStrength},
            {"waveDensity", e.waveDensity},
            {"waveSpeed", e.waveSpeed},
            {"waterDepthFade", e.waterDepthFade},
            {"waterFogDensity", e.waterFogDensity},
            {"waterFogStart", e.waterFogStart},
            {"waterFogColor", e.waterFogColor},
            {"waterSurfaceColor", e.waterSurfaceColor},
            {"waterReflectionColor", e.waterReflectionColor},
            {"waterFoamColor", e.waterFoamColor},
            {"waterFoamStrength", e.waterFoamStrength},
            {"waterFoamWidth", e.waterFoamWidth},
            {"cloudDensity", e.cloudDensity},
            {"cloudSpeed", e.cloudSpeed},
            {"skyTexturePath", e.skyTexturePath},
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
    // Keep the original v9 environment fields required, while treating newer
    // preview-only switches as optional.  Projects written before the cloud
    // controls were introduced must still open with the struct defaults.
    for (auto [name, target] : {std::pair{"enabled", &next.enabled},
                                {"sky", &next.sky},
                                {"sun", &next.sun},
                                {"atmosphere", &next.atmosphere}}) {
        if (!o[name].isBool())
            return fail();
        *target = o[name].toBool();
    }
    if (o.contains("clouds")) {
        if (!o["clouds"].isBool())
            return fail();
        next.clouds = o["clouds"].toBool();
    }
    auto number = [&](const char *name, int &target, int lo, int hi) {
        auto v = o[name];
        double n = v.toDouble();
        if (!v.isDouble() || !std::isfinite(n) || n != std::floor(n) || n < lo || n > hi)
            return false;
        target = int(n);
        return true;
    };
    // These preview-only fields were added within project v9. Older files keep
    // their strict original validation and receive the new effect defaults.
    for (auto [name, target] : {std::pair{"water", &next.water},
                                {"waterWaves", &next.waterWaves},
                                {"waterReflections", &next.waterReflections},
                                {"shadows", &next.shadows}}) {
        if (!o.contains(name))
            continue;
        if (!o[name].isBool())
            return fail();
        *target = o[name].toBool();
    }
    if (o.contains("waveStrength") && !number("waveStrength", next.waveStrength, 0, 100))
        return fail();
    if (o.contains("waveDensity") && !number("waveDensity", next.waveDensity, 1, 100))
        return fail();
    if (o.contains("waveSpeed") && !number("waveSpeed", next.waveSpeed, 0, 100))
        return fail();
    if (o.contains("waterDepthFade") && !number("waterDepthFade", next.waterDepthFade, 0, 100))
        return fail();
    next.waterFogDensity = next.waterDepthFade;
    if (o.contains("waterFogDensity") && !number("waterFogDensity", next.waterFogDensity, 0, 200))
        return fail();
    if (o.contains("waterFogStart") && !number("waterFogStart", next.waterFogStart, 0, 128))
        return fail();
    if (o.contains("waterFoamStrength") && !number("waterFoamStrength", next.waterFoamStrength, 0, 100))
        return fail();
    if (o.contains("waterFoamWidth") && !number("waterFoamWidth", next.waterFoamWidth, 1, 32))
        return fail();
    if (o.contains("cloudDensity") && !number("cloudDensity", next.cloudDensity, 0, 100))
        return fail();
    if (o.contains("cloudSpeed") && !number("cloudSpeed", next.cloudSpeed, 0, 100))
        return fail();
    if (o.contains("skyTexturePath")) {
        if (!o["skyTexturePath"].isString() || o["skyTexturePath"].toString().size() > 4096)
            return fail();
        next.skyTexturePath = o["skyTexturePath"].toString();
    }
    if (!number("skyIntensity", next.skyIntensity, 0, 200) ||
        !number("sunIntensity", next.sunIntensity, 0, 300) || !number("azimuth", next.azimuth, -180, 180) ||
        !number("elevation", next.elevation, 0, 90) || !number("haze", next.haze, 0, 100) ||
        !number("exposure", next.exposure, -30, 30))
        return fail();
    static const QRegularExpression color("^#[0-9a-fA-F]{6}$");
    for (auto [name, target] : {std::pair{"waterFogColor", &next.waterFogColor},
                                {"waterSurfaceColor", &next.waterSurfaceColor},
                                {"waterReflectionColor", &next.waterReflectionColor},
                                {"waterFoamColor", &next.waterFoamColor}}) {
        if (!o.contains(name))
            continue;
        if (!o[name].isString() || !color.match(o[name].toString()).hasMatch())
            return fail();
        *target = o[name].toString().toLower();
    }
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
    // Existing MCP clients may still use the old control name. Treat it as a
    // density alias, while an explicitly supplied new value takes precedence.
    if (changes.contains("waterDepthFade") && !changes.contains("waterFogDensity"))
        o["waterFogDensity"] = changes["waterDepthFade"];
    return loadEnvironment(o, after, error);
}
} // namespace wb
