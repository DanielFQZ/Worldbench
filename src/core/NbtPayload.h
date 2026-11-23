#pragma once
#include <QByteArray>
#include <QMap>
#include <QStringList>
#include <QVector3D>
namespace wb {
// Raw compound payload (including END, without the root tag/name). Unknown
// inventory and mod tags retain their exact NBT type and bytes.
QByteArray patchNbtPayload(const QByteArray &raw, const QMap<QString, QByteArray> &replacements,
                           const QStringList &remove = {});
QByteArray nbtInt(const QString &name, int value);
QByteArray nbtText(const QString &name, const QString &value);
QByteArray nbtVector(const QString &name, const QVector3D &value);
QByteArray transformEntityNbt(const QByteArray &raw, QVector3D position, int turn, bool mirrorX, bool mirrorZ,
                              const QByteArray &identity);
} // namespace wb
