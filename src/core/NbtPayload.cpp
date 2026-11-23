#include "NbtPayload.h"
#include <QCryptographicHash>
#include <QtEndian>
#include <cstring>
#include <stdexcept>
namespace wb {
namespace {
struct Reader {
    const QByteArray &b;
    qsizetype at = 0;
    const char *take(qsizetype n) {
        if (n < 0 || n > b.size() - at)
            throw std::runtime_error("NBT负载截断");
        auto p = b.constData() + at;
        at += n;
        return p;
    }
    template <class T> T num() { return qFromBigEndian<T>(take(sizeof(T))); }
    QString str() {
        auto n = num<quint16>();
        return QString::fromUtf8(take(n), n);
    }
    void skip(int t, int depth = 0) {
        if (depth > 64)
            throw std::runtime_error("NBT嵌套过深");
        switch (t) {
        case 1:
            take(1);
            break;
        case 2:
            take(2);
            break;
        case 3:
        case 5:
            take(4);
            break;
        case 4:
        case 6:
            take(8);
            break;
        case 7:
            take(num<qint32>());
            break;
        case 8:
            str();
            break;
        case 9: {
            int tag = num<quint8>(), n = num<qint32>();
            if (n < 0 || n > 2000000 || (!tag && n))
                throw std::runtime_error("NBT列表无效");
            for (int i = 0; i < n; ++i)
                skip(tag, depth + 1);
            break;
        }
        case 10:
            for (;;) {
                int tag = num<quint8>();
                if (!tag)
                    break;
                str();
                skip(tag, depth + 1);
            }
            break;
        case 11:
        case 12: {
            auto n = num<qint32>();
            if (n < 0 || n > 2000000)
                throw std::runtime_error("NBT数组无效");
            take(qsizetype(n) * (t == 11 ? 4 : 8));
            break;
        }
        default:
            throw std::runtime_error("NBT类型无效");
        }
    }
};
template <class T> void number(QByteArray &b, T v) {
    char p[sizeof(T)];
    qToBigEndian(v, p);
    b.append(p, sizeof(T));
}
void header(QByteArray &b, int type, const QString &name) {
    b.append(char(type));
    auto n = name.toUtf8();
    number<quint16>(b, quint16(n.size()));
    b += n;
}
} // namespace
QByteArray patchNbtPayload(const QByteArray &raw, const QMap<QString, QByteArray> &replacements,
                           const QStringList &remove) {
    Reader r{raw};
    QByteArray out;
    while (true) {
        auto start = r.at;
        int type = r.num<quint8>();
        if (!type)
            break;
        auto name = r.str();
        r.skip(type);
        if (!replacements.contains(name) && !remove.contains(name))
            out += raw.mid(start, r.at - start);
    }
    if (r.at != raw.size())
        throw std::runtime_error("NBT负载尾部多余");
    for (auto v : replacements)
        out += v;
    out.append(char(0));
    return out;
}
QByteArray nbtInt(const QString &name, int value) {
    QByteArray b;
    header(b, 3, name);
    number<qint32>(b, value);
    return b;
}
QByteArray nbtText(const QString &name, const QString &value) {
    QByteArray b;
    header(b, 8, name);
    auto s = value.toUtf8();
    number<quint16>(b, quint16(s.size()));
    b += s;
    return b;
}
QByteArray nbtVector(const QString &name, const QVector3D &value) {
    QByteArray b;
    header(b, 9, name);
    b.append(char(6));
    number<qint32>(b, 3);
    for (int i = 0; i < 3; ++i) {
        double v = value[i];
        quint64 bits;
        std::memcpy(&bits, &v, 8);
        number(b, bits);
    }
    return b;
}
QByteArray transformEntityNbt(const QByteArray &raw, QVector3D position, int turn, bool mx, bool mz,
                              const QByteArray &identity) {
    Reader r{raw};
    double yaw = 0, pitch = 0;
    while (true) {
        int type = r.num<quint8>();
        if (!type)
            break;
        auto name = r.str();
        auto at = r.at;
        if (name == "Passengers")
            throw std::runtime_error("带乘客的实体暂不支持变换，请先移除乘客关系");
        if (name == "Leash" || name == "TileX" || name == "TileY" || name == "TileZ" || name == "Facing" ||
            name == "SleepingX" || name == "SleepingY" || name == "SleepingZ")
            throw std::runtime_error("带拴绳、挂墙或睡眠锚点的实体暂不支持变换；请从源资产移除该实体后重试");
        if (name == "Rotation" && type == 9) {
            int t = r.num<quint8>(), n = r.num<qint32>();
            if (t == 5 && n == 2) {
                quint32 a = r.num<quint32>(), b = r.num<quint32>();
                float av, bv;
                std::memcpy(&av, &a, 4);
                std::memcpy(&bv, &b, 4);
                yaw = av;
                pitch = bv;
            }
        }
        r.at = at;
        r.skip(type);
    }
    if (mx)
        yaw = -yaw;
    if (mz)
        yaw = 180 - yaw;
    yaw += turn * 90;
    QByteArray rotation;
    header(rotation, 9, "Rotation");
    rotation.append(char(5));
    number<qint32>(rotation, 2);
    for (float f : {float(yaw), float(pitch)}) {
        quint32 bits;
        std::memcpy(&bits, &f, 4);
        number(rotation, bits);
    }
    QByteArray uuid;
    header(uuid, 11, "UUID");
    number<qint32>(uuid, 4);
    uuid += QCryptographicHash::hash(identity, QCryptographicHash::Sha256).left(16);
    return patchNbtPayload(raw,
                           {{"Pos", nbtVector("Pos", position)},
                            {"Motion", nbtVector("Motion", {})},
                            {"Rotation", rotation},
                            {"UUID", uuid}},
                           {"UUIDMost", "UUIDLeast"});
}
} // namespace wb
