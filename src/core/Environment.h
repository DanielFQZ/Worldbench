#pragma once
#include <QJsonObject>
#include <QString>
#include <QVector3D>

namespace wb {
struct Environment {
    bool enabled = false, sky = true, sun = true, atmosphere = true;
    bool water = true, waterWaves = true, waterReflections = true, shadows = true;
    int waveStrength = 35; // Normal perturbation only; does not move authoritative water blocks.
    int skyIntensity = 70, sunIntensity = 90, azimuth = -45, elevation = 50;
    int haze = 15, exposure = 0; // Percent and tenths of an EV, respectively.
    QString skyColor = "#90bcf2", horizonColor = "#d7e4f2", groundColor = "#52616f", sunColor = "#fff0d6";
    bool operator==(const Environment &) const = default;
    QVector3D sunDirection() const;
};
QJsonObject saveEnvironment(const Environment &environment);
bool loadEnvironment(const QJsonObject &object, Environment &environment, QString &error);
bool patchEnvironment(const Environment &before, const QJsonObject &changes, Environment &after,
                      QString &error);
} // namespace wb
