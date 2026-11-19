#include "Assets.h"
#include "World.h"
#include <QBuffer>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMatrix4x4>
#include <QPainter>
#include <QRegularExpression>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <private/qzipreader_p.h>

namespace wb {
namespace {
const QStringList directions{"east", "west", "up", "down", "south", "north"};
const QVector3D normals[] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
const float corners[6][4][3] = {
    {{1, 0, 0}, {1, 1, 0}, {1, 1, 1}, {1, 0, 1}}, {{0, 0, 1}, {0, 1, 1}, {0, 1, 0}, {0, 0, 0}},
    {{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}}, {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}},
    {{1, 0, 1}, {1, 1, 1}, {0, 1, 1}, {0, 0, 1}}, {{0, 0, 0}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0}}};
QString qualified(QString s) {
    return s.contains(':') ? s : "minecraft:" + s;
}
QVector3D vec(const QJsonValue &v, QVector3D def = {}) {
    const auto a = v.toArray();
    if (a.size() != 3)
        return def;
    return {float(a[0].toDouble()), float(a[1].toDouble()), float(a[2].toDouble())};
}
bool match(const QJsonObject &when, const QMap<QString, QString> &props) {
    for (auto it = when.begin(); it != when.end(); ++it) {
        if (it.key() == "OR" || it.key() == "AND") {
            bool any = false, all = true;
            for (const auto &v : it.value().toArray()) {
                bool m = match(v.toObject(), props);
                any |= m;
                all &= m;
            }
            if (it.key() == "OR" ? !any : !all)
                return false;
        } else if (!it.value().toString().split('|').contains(props.value(it.key())))
            return false;
    }
    return true;
}
void collectOptions(const QJsonObject &o, QMap<QString, QStringList> &options) {
    for (auto it = o.begin(); it != o.end(); ++it) {
        if (it.key() == "OR" || it.key() == "AND") {
            for (const auto &v : it.value().toArray())
                collectOptions(v.toObject(), options);
        } else
            for (const auto &value : it.value().toString().split('|'))
                if (!options[it.key()].contains(value))
                    options[it.key()].append(value);
    }
}
QJsonObject variantCondition(const QString &s) {
    QJsonObject o;
    for (const auto &entry : s.split(',', Qt::SkipEmptyParts)) {
        const auto pair = entry.split('=');
        if (pair.size() == 2)
            o[pair[0]] = pair[1];
    }
    return o;
}
QJsonObject first(const QJsonValue &v) {
    return v.isArray() ? v.toArray().first().toObject() : v.toObject();
}
QString choose(const QString &key, const QStringList &values) {
    const QMap<QString, QString> prefs{{"facing", "north"}, {"axis", "y"},           {"half", "lower"},
                                       {"type", "bottom"},  {"shape", "straight"},   {"hinge", "left"},
                                       {"face", "floor"},   {"attachment", "floor"}, {"part", "foot"},
                                       {"mode", "compare"}};
    for (const auto &v : QStringList{prefs.value(key), "false", "none", "0", "bottom", "single"})
        if (values.contains(v))
            return v;
    auto sorted = values;
    std::sort(sorted.begin(), sorted.end());
    return sorted.value(0);
}
} // namespace
QString BlockState::label() const {
    QStringList p;
    for (auto i = properties.begin(); i != properties.end(); ++i)
        p << i.key() + "=" + i.value();
    return name + (p.isEmpty() ? QString() : "[" + p.join(',') + "]");
}
QImage whiteAtlas() {
    QImage i(1, 1, QImage::Format_RGBA8888);
    i.fill(Qt::white);
    return i;
}
std::shared_ptr<const BlockModel> fallbackModel(int b) {
    auto m = std::make_shared<BlockModel>();
    if (b == Air)
        return m;
    const auto &c = info(Block(b));
    m->full = b != Slab;
    m->translucent = b == Glass || b == Water;
    m->fallback = b >= BlockCount;
    for (int f = 0; f < 6; ++f) {
        ModelFace face;
        face.normal = normals[f];
        face.cull = (b == Slab && f == 2) ? -1 : f;
        face.color = b < BlockCount ? QVector4D(c.r, c.g, c.b, c.a) : QVector4D(1, 0.1f, 0.8f, 1);
        for (int p = 0; p < 4; ++p) {
            face.points[p] = {corners[f][p][0], corners[f][p][1] * (b == Slab ? .5f : 1.f), corners[f][p][2]};
            face.uv[p] = {0, 0};
        }
        m->faces.push_back(face);
    }
    if (m->fallback)
        m->diagnostic = "未找到可渲染的 JSON 模型（可能使用游戏专用渲染器）";
    return m;
}
std::shared_ptr<AssetLibrary> AssetLibrary::load(const QStringList &paths, QString &error) {
    auto lib = std::make_shared<AssetLibrary>();
    lib->sources = paths;
    QMap<QString, QByteArray> files;
    QJsonObject lang;
    qint64 budget = 0;
    auto fail = [&](QString s) -> std::shared_ptr<AssetLibrary> {
        error = s;
        return {};
    };
    if (paths.isEmpty() || paths.size() > 8)
        return fail("请选择 1–8 个资源来源");
    const QRegularExpression safe("^[a-z0-9_./-]+$");
    for (int sourceIndex = 0; sourceIndex < paths.size(); ++sourceIndex) {
        const QString path = paths[sourceIndex];
        if (QFileInfo(path).size() > 512 * 1024 * 1024)
            return fail("资源文件超过 512 MiB");
        QZipReader zip(path);
        if (!zip.isReadable())
            return fail("无法读取 ZIP/JAR：" + path);
        const auto entries = zip.fileInfoList();
        if (entries.size() > 100000)
            return fail("压缩包条目过多");
        for (const auto &e : entries) {
            const auto &n = e.filePath;
            if (n == "version.json" && sourceIndex == 0 && e.size < 65536)
                lib->version = QJsonDocument::fromJson(zip.fileData(n)).object()["id"].toString();
            if (!e.isFile || !n.startsWith("assets/"))
                continue;
            if (n.contains("..") || n.contains('\\') || !safe.match(n).hasMatch())
                return fail("资源路径不合法：" + n);
            if (!(n.endsWith(".png") || n.endsWith(".json")))
                continue;
            if (!(n.contains("/blockstates/") || n.contains("/models/") || n.contains("/textures/") ||
                  n.endsWith("/lang/zh_cn.json") || n.endsWith("/lang/en_us.json")))
                continue;
            if (e.size < 0 || e.size > 16 * 1024 * 1024 || (budget += e.size) > 256 * 1024 * 1024)
                return fail("资源解压预算超限（单项 16 MiB / 总量 256 MiB）");
            auto data = zip.fileData(n);
            if (data.size() != e.size || zip.status() != QZipReader::NoError)
                return fail("资源解压失败：" + n);
            files[n] = data;
        }
        // A pack may replace rendering assets, but must not create registry entries on top of a JAR.
        if (sourceIndex == 0)
            for (auto it = files.begin(); it != files.end(); ++it) {
                const auto parts = it.key().split('/');
                if (parts.size() == 4 && parts[2] == "blockstates" && parts[3].endsWith(".json")) {
                    const QString name = parts[1] + ":" + parts[3].chopped(5);
                    lib->catalog[name].name = name;
                }
            }
    }
    QMap<QString, QImage> images;
    for (auto it = files.begin(); it != files.end(); ++it) {
        const auto parts = it.key().split('/');
        if (parts.size() < 4)
            continue;
        const auto ns = parts[1];
        if (parts[2] == "lang") {
            auto o = QJsonDocument::fromJson(it.value()).object();
            for (auto p = o.begin(); p != o.end(); ++p)
                lang[p.key()] = p.value();
        } else if (parts[2] == "textures" && it.key().endsWith(".png")) {
            QBuffer buffer;
            buffer.setData(it.value());
            buffer.open(QIODevice::ReadOnly);
            QImageReader reader(&buffer, "png");
            const auto sz = reader.size();
            if (sz.width() < 1 || sz.height() < 1 || qint64(sz.width()) * sz.height() > 16 * 1024 * 1024)
                return fail("贴图尺寸超限：" + it.key());
            QImage image = reader.read();
            if (image.isNull())
                return fail("PNG 无效：" + it.key());
            int side = std::min(image.width(), image.height());
            image =
                image.copy(0, 0, side, side).scaled(32, 32, Qt::IgnoreAspectRatio, Qt::FastTransformation);
            images[ns + ":" + parts.mid(3).join('/').chopped(4)] = image;
        } else if (parts[2] == "models" || parts[2] == "blockstates") {
            QJsonParseError parse;
            auto doc = QJsonDocument::fromJson(it.value(), &parse);
            if (parse.error != QJsonParseError::NoError || !doc.isObject())
                return fail("模型 JSON 无效：" + it.key());
            const auto id = ns + ":" + parts.mid(3).join('/').chopped(5);
            if (parts[2] == "models")
                lib->m_models[id] = doc.object();
            else
                lib->m_states[id] = doc.object();
        }
    }
    if (images.size() > 6000)
        return fail("贴图数量超过 6000");
    int side = 1, grid = int(std::ceil(std::sqrt(images.size() + 2.0)));
    while (side < grid * 34)
        side *= 2;
    lib->atlas = QImage(side, side, QImage::Format_RGBA8888);
    lib->atlas.fill(Qt::white);
    QPainter painter(&lib->atlas);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    int tile = 1;
    for (auto it = images.begin(); it != images.end(); ++it, ++tile) {
        int x = (tile % grid) * 34 + 1, y = (tile / grid) * 34 + 1;
        painter.drawImage(x, y, it.value());
        painter.drawImage(QRect(x - 1, y, 1, 32), it.value(), QRect(0, 0, 1, 32));
        painter.drawImage(QRect(x + 32, y, 1, 32), it.value(), QRect(31, 0, 1, 32));
        painter.drawImage(QRect(x, y - 1, 32, 1), it.value(), QRect(0, 0, 32, 1));
        painter.drawImage(QRect(x, y + 32, 32, 1), it.value(), QRect(0, 31, 32, 1));
        lib->m_uv[it.key()] = QRectF(double(x) / side, double(y) / side, 32.0 / side, 32.0 / side);
    }
    painter.end();
    lib->thumbnails = images;
    for (auto it = lib->catalog.begin(); it != lib->catalog.end(); ++it) {
        auto &b = it.value();
        b.defaults.name = it.key();
        b.title = lang.value("block." + QString(it.key()).replace(':', '.')).toString(it.key());
        auto o = lib->m_states.value(it.key());
        auto variants = o["variants"].toObject();
        for (auto v = variants.begin(); v != variants.end(); ++v)
            collectOptions(variantCondition(v.key()), b.options);
        for (const auto &v : o["multipart"].toArray())
            collectOptions(v.toObject()["when"].toObject(), b.options);
        for (auto p = b.options.begin(); p != b.options.end(); ++p) {
            if (p.value().contains("true") && !p.value().contains("false"))
                p.value() << "false";
            b.defaults.properties[p.key()] = choose(p.key(), p.value());
        }
    }
    if (lib->catalog.isEmpty() && lib->version.isEmpty())
        lib->warnings << "资源包未包含方块状态；仅覆盖已有基础材质。";
    return lib;
}
QJsonObject AssetLibrary::inherited(const QString &name, QStringList chain) const {
    if (chain.size() > 32 || chain.contains(name))
        return {};
    chain << name;
    auto child = m_models.value(name);
    QJsonObject result;
    if (child.contains("parent"))
        result = inherited(qualified(child["parent"].toString()), chain);
    auto textures = result["textures"].toObject();
    auto own = child["textures"].toObject();
    for (auto i = own.begin(); i != own.end(); ++i)
        textures[i.key()] = i.value();
    for (auto i = child.begin(); i != child.end(); ++i)
        result[i.key()] = i.value();
    result["textures"] = textures;
    return result;
}
bool AssetLibrary::validate(const BlockState &state, QString &error) const {
    auto it = catalog.find(state.name);
    if (it == catalog.end()) {
        error = "方块不在已加载目录中：" + state.name;
        return false;
    }
    for (auto p = state.properties.begin(); p != state.properties.end(); ++p) {
        if (it->options.contains(p.key()) && !it->options[p.key()].contains(p.value())) {
            error = "无效方块状态：" + state.label();
            return false;
        }
        if (!it->options.contains(p.key()) &&
            !(p.key() == "waterlogged" && (p.value() == "false" || p.value() == "true")) &&
            !(state.name == "minecraft:water" && p.key() == "level" && p.value() == "0")) {
            error = "资源中没有该状态属性：" + p.key();
            return false;
        }
    }
    return true;
}
std::shared_ptr<const BlockModel> AssetLibrary::model(const BlockState &state) const {
    if (state.name == "minecraft:air" || state.name.endsWith(":cave_air") || state.name.endsWith(":void_air"))
        return std::make_shared<BlockModel>();
    auto m = std::make_shared<BlockModel>();
    const auto bs = m_states.value(state.name);
    QList<QJsonObject> applied;
    auto props = catalog.value(state.name).defaults.properties;
    for (auto i = state.properties.begin(); i != state.properties.end(); ++i)
        props[i.key()] = i.value();
    auto variants = bs["variants"].toObject();
    for (auto i = variants.begin(); i != variants.end(); ++i)
        if (match(variantCondition(i.key()), props)) {
            applied << first(i.value());
            break;
        }
    for (const auto &part : bs["multipart"].toArray()) {
        auto o = part.toObject();
        if (match(o["when"].toObject(), props))
            applied << first(o["apply"]);
    }
    // Texture-only packs can still re-skin the built-in cube materials.
    if (applied.isEmpty() && bs.isEmpty()) {
        QString path = state.name;
        path.insert(path.indexOf(':') + 1, "block/");
        if (m_models.contains(path))
            applied << QJsonObject{{"model", path}};
    }
    if (applied.isEmpty() && bs.isEmpty() && state.name != "minecraft:water") {
        for (int b = 0; b < BlockCount; ++b)
            if (state.name == QString::fromUtf8(info(b).minecraft)) {
                auto basic = std::make_shared<BlockModel>(*fallbackModel(b));
                for (int face = 0; face < int(basic->faces.size()); ++face) {
                    QString tex = state.name;
                    tex.insert(tex.indexOf(':') + 1, "block/");
                    if (b == Slab)
                        tex = "minecraft:block/oak_planks";
                    if (b == Grass)
                        tex = face == 2   ? "minecraft:block/grass_block_top"
                              : face == 3 ? "minecraft:block/dirt"
                                          : "minecraft:block/grass_block_side";
                    const auto rect = m_uv.value(tex);
                    if (rect.isEmpty())
                        continue;
                    auto &f = basic->faces[face];
                    f.color = {1, 1, 1, b == Glass ? .65f : 1};
                    if (b == Grass && face == 2)
                        f.color = {.55f, .78f, .35f, 1};
                    for (int p = 0; p < 4; ++p)
                        f.uv[p] = {float(rect.x() + ((p == 2 || p == 3) ? 1 : 0) * rect.width()),
                                   float(rect.y() + ((p == 0 || p == 3) ? 1 : 0) * rect.height())};
                    if (basic->thumbnail.isEmpty())
                        basic->thumbnail = tex;
                }
                return basic;
            }
    }
    const bool water = state.name == "minecraft:water" || state.name == "minecraft:lava";
    if (water) {
        auto f = std::make_shared<BlockModel>(*fallbackModel(state.name.endsWith(":water") ? Water : Stone));
        auto tex = state.name.endsWith(":water") ? QString("minecraft:block/water_still")
                                                 : QString("minecraft:block/lava_still");
        auto rect = m_uv.value(tex);
        f->thumbnail = tex;
        if (!rect.isEmpty())
            for (auto &face : f->faces) {
                for (int i = 0; i < 4; ++i)
                    face.uv[i] = {float(rect.x() + ((i == 2 || i == 3) ? 1 : 0) * rect.width()),
                                  float(rect.y() + ((i == 0 || i == 3) ? 1 : 0) * rect.height())};
                face.color =
                    state.name.endsWith(":water") ? QVector4D(.3f, .55f, 1, .72f) : QVector4D(1, 1, 1, 1);
            }
        return f;
    }
    m->translucent = state.name.contains("glass") && !state.name.contains("pane") ||
                     state.name.contains("ice") || state.name.contains("slime") ||
                     state.name.contains("honey_block");
    for (const auto &application : applied) {
        auto model = inherited(qualified(application["model"].toString()));
        auto textures = model["textures"].toObject();
        auto resolve = [&](QString tex) {
            for (int i = 0; i < 32 && tex.startsWith('#'); ++i)
                tex = textures[tex.mid(1)].toString();
            return qualified(tex);
        };
        const auto particle = resolve(textures["particle"].toString());
        if (m->thumbnail.isEmpty() && m_uv.contains(particle))
            m->thumbnail = particle;
        QMatrix4x4 blockRotation;
        blockRotation.translate(.5f, .5f, .5f);
        blockRotation.rotate(float(-(application["y"].toInt() % 360)), 0, 1, 0);
        blockRotation.rotate(float(-(application["x"].toInt() % 360)), 1, 0, 0);
        blockRotation.translate(-.5f, -.5f, -.5f);
        for (const auto &ev : model["elements"].toArray()) {
            if (m->faces.size() > 8192) {
                m->diagnostic = "模型面数超过预算";
                break;
            }
            const auto e = ev.toObject();
            auto lo = vec(e["from"]) / 16.f, hi = vec(e["to"]) / 16.f;
            if (!std::isfinite(lo.lengthSquared()) || !std::isfinite(hi.lengthSquared()) || lo.length() > 8 ||
                hi.length() > 8)
                continue;
            QMatrix4x4 transform;
            auto rot = e["rotation"].toObject();
            if (!rot.isEmpty()) {
                auto origin = vec(rot["origin"]) / 16.f;
                const QString axis = rot["axis"].toString();
                const float angle = float(std::clamp(rot["angle"].toDouble(), -45., 45.));
                transform.translate(origin);
                transform.rotate(angle, axis == "x", axis == "y", axis == "z");
                if (rot["rescale"].toBool()) {
                    float s = 1.f / std::max(.5f, std::abs(std::cos(qDegreesToRadians(angle))));
                    transform.scale(axis == "x" ? 1 : s, axis == "y" ? 1 : s, axis == "z" ? 1 : s);
                }
                transform.translate(-origin);
            }
            transform = blockRotation * transform;
            const auto faces = e["faces"].toObject();
            for (int f = 0; f < 6; ++f) {
                if (!faces.contains(directions[f]))
                    continue;
                const auto face = faces[directions[f]].toObject();
                ModelFace out;
                auto tex = resolve(face["texture"].toString());
                auto rect = m_uv.value(tex);
                if (m->thumbnail.isEmpty() && !rect.isEmpty())
                    m->thumbnail = tex;
                if (rect.isEmpty()) {
                    out.color = {1, .05f, .8f, 1};
                    m->diagnostic = "部分纹理缺失";
                }
                if (face.contains("tintindex"))
                    out.color = {.55f, .78f, .35f, 1};
                if (state.name.contains("glass") && m->translucent)
                    out.color.setW(.65f);
                auto uv = face["uv"].toArray();
                float u0 = 0, v0 = 0, u1 = 16, v1 = 16;
                if (f == 0) {
                    u0 = (1 - hi.z()) * 16;
                    u1 = (1 - lo.z()) * 16;
                    v0 = (1 - hi.y()) * 16;
                    v1 = (1 - lo.y()) * 16;
                }
                if (f == 1) {
                    u0 = lo.z() * 16;
                    u1 = hi.z() * 16;
                    v0 = (1 - hi.y()) * 16;
                    v1 = (1 - lo.y()) * 16;
                }
                if (f == 2) {
                    u0 = lo.x() * 16;
                    u1 = hi.x() * 16;
                    v0 = lo.z() * 16;
                    v1 = hi.z() * 16;
                }
                if (f == 3) {
                    u0 = lo.x() * 16;
                    u1 = hi.x() * 16;
                    v0 = (1 - hi.z()) * 16;
                    v1 = (1 - lo.z()) * 16;
                }
                if (f == 4) {
                    u0 = lo.x() * 16;
                    u1 = hi.x() * 16;
                    v0 = (1 - hi.y()) * 16;
                    v1 = (1 - lo.y()) * 16;
                }
                if (f == 5) {
                    u0 = (1 - hi.x()) * 16;
                    u1 = (1 - lo.x()) * 16;
                    v0 = (1 - hi.y()) * 16;
                    v1 = (1 - lo.y()) * 16;
                }
                if (uv.size() == 4) {
                    u0 = float(uv[0].toDouble());
                    v0 = float(uv[1].toDouble());
                    u1 = float(uv[2].toDouble());
                    v1 = float(uv[3].toDouble());
                }
                // Face vertices run bottom-left, top-left, top-right, bottom-right in model space.
                QVector2D coords[4] = {{u1, v1}, {u1, v0}, {u0, v0}, {u0, v1}};
                if (f == 2 || f == 3) {
                    coords[0] = {u0, v1};
                    coords[1] = {u1, v1};
                    coords[2] = {u1, v0};
                    coords[3] = {u0, v0};
                }
                int turn = ((face["rotation"].toInt() / 90) % 4 + 4) % 4;
                for (int p = 0; p < 4; ++p) {
                    out.points[p] = transform.map(lo + QVector3D(corners[f][p][0] * (hi.x() - lo.x()),
                                                                 corners[f][p][1] * (hi.y() - lo.y()),
                                                                 corners[f][p][2] * (hi.z() - lo.z())));
                    auto c = coords[(p + turn) % 4] / 16.f;
                    if (application["uvlock"].toBool()) {
                        const QVector3D axisU[] = {{0, 0, -1}, {0, 0, 1}, {1, 0, 0},
                                                   {1, 0, 0},  {1, 0, 0}, {-1, 0, 0}};
                        const QVector3D axisV[] = {{0, -1, 0}, {0, -1, 0}, {0, 0, 1},
                                                   {0, 0, -1}, {0, -1, 0}, {0, -1, 0}};
                        auto n = blockRotation.mapVector(normals[f]);
                        int target = f;
                        for (int d = 0; d < 6; ++d)
                            if (QVector3D::dotProduct(n, normals[d]) > .999f)
                                target = d;
                        auto u = blockRotation.mapVector(axisU[f]), v = blockRotation.mapVector(axisV[f]);
                        auto centered = c - QVector2D(.5f, .5f);
                        c = {.5f + QVector3D::dotProduct(u, axisU[target]) * centered.x() +
                                 QVector3D::dotProduct(v, axisU[target]) * centered.y(),
                             .5f + QVector3D::dotProduct(u, axisV[target]) * centered.x() +
                                 QVector3D::dotProduct(v, axisV[target]) * centered.y()};
                    }
                    out.uv[p] = {float(rect.x() + c.x() * rect.width()),
                                 float(rect.y() + c.y() * rect.height())};
                }
                out.normal =
                    QVector3D::crossProduct(out.points[1] - out.points[0], out.points[2] - out.points[0])
                        .normalized();
                auto expected = transform.mapVector(normals[f]).normalized();
                if (QVector3D::dotProduct(out.normal, expected) < 0)
                    out.normal = -out.normal;
                int cull = directions.indexOf(face["cullface"].toString());
                if (cull >= 0) {
                    auto n = blockRotation.mapVector(normals[cull]);
                    for (int d = 0; d < 6; ++d)
                        if (QVector3D::dotProduct(n, normals[d]) > .999f)
                            out.cull = d;
                }
                m->faces.push_back(out);
            }
            if (lo == QVector3D(0, 0, 0) && hi == QVector3D(1, 1, 1) && rot.isEmpty() && faces.size() == 6)
                m->full = true;
        }
    }
    if (m->faces.empty()) {
        auto fallback = std::make_shared<BlockModel>(*fallbackModel(BlockCount));
        fallback->thumbnail = m->thumbnail;
        return fallback;
    }
    // Alpha-cutout textures (leaves, plants, glass panes) cannot occlude adjacent blocks.
    if (state.name.contains("leaves") || state.name.contains("glass") || m->translucent)
        m->full = false;
    return m;
}
} // namespace wb
