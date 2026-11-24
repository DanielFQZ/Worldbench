#include "WorldCreation.h"
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace wb {
QMatrix4x4 placementMatrix(const AssetPreset &p) {
    QMatrix4x4 m;
    m.translate(p.position + QVector3D(.5, .5, .5));
    m.rotate(-p.rotation.y(), 0, 1, 0);
    m.scale(p.scale.x() < 0 ? -1 : 1, 1, p.scale.z() < 0 ? -1 : 1);
    m.translate(-.5, -.5, -.5);
    m.scale(std::abs(p.scale.x()), p.scale.y(), std::abs(p.scale.z()));
    return m;
}

namespace {
QJsonArray vec3(const QVector3D &v) {
    return {v.x(), v.y(), v.z()};
}
bool readVec3(const QJsonValue &value, QVector3D &out) {
    const auto a = value.toArray();
    if (a.size() != 3)
        return false;
    for (const auto &v : a)
        if (!v.isDouble() || !std::isfinite(v.toDouble()) || std::abs(v.toDouble()) > 1e9)
            return false;
    out = {float(a[0].toDouble()), float(a[1].toDouble()), float(a[2].toDouble())};
    return true;
}
bool id(const QString &s) {
    return !s.isEmpty() && s.size() <= 96 && !s.contains(QRegularExpression("[^A-Za-z0-9_.-]"));
}
double hash01(double x, double z, uint32_t seed) {
    uint64_t n = uint64_t(std::llround(x * 4096)) * 0x9e3779b185ebca87ull ^
                 uint64_t(std::llround(z * 4096)) * 0xc2b2ae3d27d4eb4full ^ seed;
    n ^= n >> 30;
    n *= 0xbf58476d1ce4e5b9ull;
    n ^= n >> 27;
    n *= 0x94d049bb133111ebull;
    n ^= n >> 31;
    return double(n & 0x1fffffull) / double(0x200000);
}
QJsonObject base(const QString &idValue, const QString &name) {
    return {{"id", idValue}, {"name", name}};
}
bool text(const QJsonObject &o, const char *key, QString &out, bool required = true) {
    if (!o.contains(key))
        return !required;
    if (!o[key].isString() || o[key].toString().size() > 4096)
        return false;
    out = o[key].toString();
    return true;
}
} // namespace

bool PcgGraph::validate(QString &error) const {
    PcgPlan plan;
    return compileGraph(*this, plan, error);
}
double PcgGraph::evaluate(double x, double z, double height, double slope, uint32_t seed,
                          QString *error) const {
    PcgPlan p;
    QString e;
    if (!compileGraph(*this, p, e)) {
        if (error)
            *error = e;
        return 0;
    }
    return p.evaluate({x, z, height, height, slope, 0, 0, 1, seed});
}

QJsonObject PcgGraph::toJson() const {
    QJsonArray ns, cs;
    for (const auto &n : nodes)
        ns.append(QJsonObject{{"id", n.id}, {"type", n.type}, {"parameters", n.parameters}});
    for (const auto &c : connections)
        cs.append(QJsonObject{
            {"fromNode", c.fromNode}, {"fromPort", c.fromPort}, {"toNode", c.toNode}, {"toPort", c.toPort}});
    return {{"id", id},           {"name", name}, {"version", version}, {"surfaceMaterial", surfaceMaterial},
            {"enabled", enabled}, {"nodes", ns},  {"connections", cs}};
}
bool PcgGraph::fromJson(const QJsonObject &o, PcgGraph &out, QString &error) {
    if (!text(o, "id", out.id) || !text(o, "name", out.name, false) || !o["nodes"].isArray() ||
        !o["connections"].isArray()) {
        error = "PCG 图数据无效";
        return false;
    }
    out.version = o["version"].toInt(1);
    out.enabled = o["enabled"].toBool(true);
    out.surfaceMaterial = o["surfaceMaterial"].toBool();
    out.nodes.clear();
    out.connections.clear();
    for (const auto &v : o["nodes"].toArray()) {
        const auto n = v.toObject();
        PcgNode node{n["id"].toString(), n["type"].toString(), n["parameters"].toObject()};
        out.nodes.push_back(node);
    }
    for (const auto &v : o["connections"].toArray()) {
        const auto c = v.toObject();
        out.connections.push_back({c["fromNode"].toString(), c["fromPort"].toString(), c["toNode"].toString(),
                                   c["toPort"].toString()});
    }
    if (out.version == 1) {
        const auto has = [&](QStringView type) {
            return std::any_of(out.nodes.begin(), out.nodes.end(),
                               [&](const auto &n) { return n.type == type; });
        };
        bool material = has(u"output_surface"), scatter = has(u"output_scatter");
        if (material != scatter)
            out.surfaceMaterial = material;
    }
    return out.validate(error);
}

QMatrix4x4 PcgVolume::orientation() const {
    QMatrix4x4 m;
    if (!rotation.isNull()) {
        const auto center = (min + max) * .5f;
        m.translate(center);
        m.rotate(rotation.y(), 0, 1, 0);
        m.rotate(rotation.x(), 1, 0, 0);
        m.rotate(rotation.z(), 0, 0, 1);
        m.translate(-center);
    }
    return m;
}
QRectF PcgVolume::footprint() const {
    const auto pose = orientation();
    auto lo = pose.map(min), hi = lo;
    for (int i = 0; i < 8; ++i) {
        auto p = pose.map(
            QVector3D(i & 1 ? max.x() : min.x(), i & 2 ? max.y() : min.y(), i & 4 ? max.z() : min.z()));
        lo.setX(std::min(lo.x(), p.x()));
        lo.setZ(std::min(lo.z(), p.z()));
        hi.setX(std::max(hi.x(), p.x()));
        hi.setZ(std::max(hi.z(), p.z()));
    }
    return {lo.x(), lo.z(), hi.x() - lo.x(), hi.z() - lo.z()};
}
QJsonObject PcgVolume::toJson() const {
    return {{"id", id},
            {"name", name},
            {"graphId", graphId},
            {"min", vec3(min)},
            {"max", vec3(max)},
            {"rotation", vec3(rotation)},
            {"seedDomain", seedDomain},
            {"transition", transition},
            {"seed", double(seed)},
            {"enabled", enabled},
            {"visible", visible},
            {"sourceId", sourceId},
            {"contributionOnly", contributionOnly},
            {"overrides", overrides}};
}
bool PcgVolume::fromJson(const QJsonObject &o, PcgVolume &out, QString &error) {
    if (!text(o, "id", out.id) || !text(o, "name", out.name, false) ||
        !text(o, "graphId", out.graphId, false) || !readVec3(o["min"], out.min) ||
        !readVec3(o["max"], out.max) ||
        !readVec3(o.contains("rotation") ? o.value("rotation") : QJsonValue(QJsonArray{0, 0, 0}),
                  out.rotation) ||
        !o["transition"].isDouble()) {
        error = "PCG 范围数据无效";
        return false;
    }
    out.transition = std::clamp(float(o["transition"].toDouble()), 0.f, 4096.f);
    double seed = o["seed"].toDouble(1);
    if (!std::isfinite(seed) || seed < 0 || seed > 4294967295. || std::floor(seed) != seed) {
        error = "种子必须为0…4294967295的整数";
        return false;
    }
    out.seed = uint32_t(seed);
    out.seedDomain = o["seedDomain"].toString();
    out.overrides = o["overrides"].toObject();
    out.enabled = o["enabled"].toBool(true);
    out.visible = o["visible"].toBool(true);
    out.sourceId = o["sourceId"].toString();
    out.contributionOnly = o["contributionOnly"].toBool(true);
    return true;
}

QJsonObject StructureAsset::toJson(bool includeBlocks) const {
    QJsonArray b, m, entitiesJson;
    for (auto &e : entities)
        entitiesJson.append(
            QJsonObject{{"position", vec3(e.position)}, {"nbt", QString::fromLatin1(e.nbt.toBase64())}});
    for (auto &v : markers)
        m.append(v);
    if (includeBlocks)
        for (auto &v : blocks)
            b.append(QJsonObject{{"x", v.x},
                                 {"y", v.y},
                                 {"z", v.z},
                                 {"state", v.state},
                                 {"nbt", QString::fromLatin1(v.nbt.toBase64())}});
    return {{"id", id},
            {"name", name},
            {"sourcePath", sourcePath},
            {"format", format},
            {"width", width},
            {"height", height},
            {"depth", depth},
            {"pivot", vec3(pivot)},
            {"groundBlend", groundBlend},
            {"category", category},
            {"placement", placement},
            {"markers", m},
            {"blocks", b},
            {"entities", entitiesJson},
            {"diagnostic", diagnostic},
            {"defaultRotation", vec3(defaultRotation)},
            {"defaultScale", vec3(defaultScale)}};
}
bool StructureAsset::fromJson(const QJsonObject &o, StructureAsset &out, QString &error) {
    if (!text(o, "id", out.id) || !text(o, "name", out.name, false) ||
        !text(o, "sourcePath", out.sourcePath, false) || !text(o, "format", out.format, false) ||
        !readVec3(o["pivot"], out.pivot)) {
        error = "结构资产数据无效";
        return false;
    }
    out.width = o["width"].toInt();
    out.height = o["height"].toInt();
    out.depth = o["depth"].toInt();
    out.groundBlend = o["groundBlend"].toInt(0);
    out.placement = o["placement"].toString("place");
    for (const auto &v : o["markers"].toArray())
        out.markers << v.toString();
    out.blocks.clear();
    for (const auto &v : o["blocks"].toArray()) {
        auto b = v.toObject();
        out.blocks.push_back({b["x"].toInt(), b["y"].toInt(), b["z"].toInt(), b["state"].toString(),
                              QByteArray::fromBase64(b["nbt"].toString().toLatin1())});
    }
    out.entities.clear();
    for (auto e : o["entities"].toArray()) {
        StructureEntity entity;
        if (!readVec3(e.toObject()["position"], entity.position)) {
            error = "实体坐标无效";
            return false;
        }
        entity.nbt = QByteArray::fromBase64(e.toObject()["nbt"].toString().toLatin1());
        out.entities.push_back(entity);
    }
    out.category = o["category"].toString();
    if (out.category.isEmpty() && std::any_of(out.blocks.begin(), out.blocks.end(), [](const auto &b) {
            return b.state.section('[', 0, 0).endsWith("_leaves");
        }))
        out.category = "plant";
    if (out.category == "plant" || out.category == "building" || out.category == "cave" ||
        out.category.isEmpty())
        out.category = out.isOpen() ? "open" : "closed";
    out.diagnostic = o["diagnostic"].toString();
    if (o.contains("defaultRotation") && !readVec3(o["defaultRotation"], out.defaultRotation)) {
        error = "资产默认旋转无效";
        return false;
    }
    if (o.contains("defaultScale") && !readVec3(o["defaultScale"], out.defaultScale)) {
        error = "资产默认缩放无效";
        return false;
    }
    return out.width >= 0 && out.width <= 4096 && out.height >= 0 && out.height <= 4096 && out.depth >= 0 &&
           out.depth <= 4096;
}
QJsonObject AssetPreset::toJson() const {
    return {{"id", id},
            {"name", name},
            {"assetId", assetId},
            {"filePath", filePath},
            {"position", vec3(position)},
            {"rotation", vec3(rotation)},
            {"scale", vec3(scale)},
            {"enabled", enabled}};
}
bool AssetPreset::fromJson(const QJsonObject &o, AssetPreset &out, QString &error) {
    if (!text(o, "id", out.id) || !text(o, "name", out.name, false) || !text(o, "assetId", out.assetId) ||
        !readVec3(o["position"], out.position) || !readVec3(o["rotation"], out.rotation) ||
        !readVec3(o["scale"], out.scale)) {
        error = "资产预设数据无效";
        return false;
    }
    out.filePath = o["filePath"].toString();
    out.enabled = o["enabled"].toBool(true);
    return true;
}
QJsonObject RoadNode::toJson() const {
    return {{"id", id}, {"name", name}, {"position", vec3(position)}, {"enabled", enabled}};
}
bool RoadNode::fromJson(const QJsonObject &o, RoadNode &out, QString &error) {
    if (!text(o, "id", out.id) || !text(o, "name", out.name, false) ||
        !readVec3(o["position"], out.position)) {
        error = "道路节点数据无效";
        return false;
    }
    out.enabled = o["enabled"].toBool(true);
    return true;
}
QJsonObject RoadEdge::toJson() const {
    return {{"id", id},
            {"from", from},
            {"to", to},
            {"width", width},
            {"surface", surface},
            {"bridge", bridge},
            {"tunnel", tunnel},
            {"enabled", enabled},
            {"maxSlope", maxSlope},
            {"clearance", clearance}};
}
bool RoadEdge::fromJson(const QJsonObject &o, RoadEdge &out, QString &error) {
    if (!text(o, "id", out.id) || !text(o, "from", out.from) || !text(o, "to", out.to) ||
        !text(o, "surface", out.surface, false)) {
        error = "道路连接数据无效";
        return false;
    }
    out.width = std::clamp(o["width"].toInt(5), 1, 64);
    out.bridge = o["bridge"].toBool(false);
    out.tunnel = o["tunnel"].toBool(false);
    out.maxSlope = float(o["maxSlope"].toDouble(30));
    out.clearance = o["clearance"].toInt(5);
    out.enabled = o["enabled"].toBool(true);
    return true;
}
QJsonObject CaveMarker::toJson() const {
    return {{"id", id},         {"assetId", assetId}, {"kind", kind}, {"position", vec3(position)},
            {"end", vec3(end)}, {"enabled", enabled}};
}
bool CaveMarker::fromJson(const QJsonObject &o, CaveMarker &out, QString &error) {
    if (!text(o, "id", out.id) || !text(o, "assetId", out.assetId) || !text(o, "kind", out.kind) ||
        !readVec3(o["position"], out.position)) {
        error = "洞穴标记数据无效";
        return false;
    }
    if (o.contains("end"))
        readVec3(o["end"], out.end);
    else
        out.end = out.position;
    out.enabled = o["enabled"].toBool(true);
    return true;
}

QJsonObject saveCreation(const CreationState &s) {
    QJsonArray g, v, a, p, n, e, c;
    for (auto &x : s.graphs)
        g.append(x.toJson());
    for (auto &x : s.volumes)
        v.append(x.toJson());
    for (auto &x : s.assets)
        a.append(x.toJson());
    for (auto &x : s.presets)
        p.append(x.toJson());
    for (auto &x : s.roadNodes)
        n.append(x.toJson());
    for (auto &x : s.roadEdges)
        e.append(x.toJson());
    for (auto &x : s.caveMarkers)
        c.append(x.toJson());
    QJsonArray library;
    for (const auto &asset : s.pcgAssets)
        library.append(asset.toJson());
    QJsonObject foldersByAsset;
    for (auto it = s.assetFolders.cbegin(); it != s.assetFolders.cend(); ++it)
        foldersByAsset[it.key()] = it.value();
    return {{"folders", QJsonArray::fromStringList(s.folders)},
            {"assetFolders", foldersByAsset},
            {"pcgAssets", library},
            {"version", 1},
            {"graphs", g},
            {"volumes", v},
            {"assets", a},
            {"presets", p},
            {"roadNodes", n},
            {"roadEdges", e},
            {"caveMarkers", c}};
}
bool loadCreation(const QJsonObject &o, CreationState &s, QString &error) {
    if (o.isEmpty()) {
        s = {};
        return true;
    }
    auto arr = [&](const char *k) { return o[k].isArray() && o[k].toArray().size() <= 4096; };
    for (auto k : {"graphs", "volumes", "assets", "presets", "roadNodes", "roadEdges", "caveMarkers"})
        if (!arr(k)) {
            error = "创作数据数组无效";
            return false;
        }
    s = {};
    for (auto f : o["folders"].toArray())
        s.folders.append(f.toString());
    const auto locations = o["assetFolders"].toObject();
    for (auto it = locations.begin(); it != locations.end(); ++it)
        s.assetFolders.insert(it.key(), it.value().toString());
    for (const auto &v : o["graphs"].toArray()) {
        PcgGraph x;
        if (!PcgGraph::fromJson(v.toObject(), x, error))
            return false;
        s.graphs.push_back(std::move(x));
    }
    for (const auto &v : o["volumes"].toArray()) {
        PcgVolume x;
        if (!PcgVolume::fromJson(v.toObject(), x, error))
            return false;
        s.volumes.push_back(std::move(x));
    }
    for (const auto &v : o["assets"].toArray()) {
        StructureAsset x;
        if (!StructureAsset::fromJson(v.toObject(), x, error))
            return false;
        s.assets.push_back(std::move(x));
    }
    for (const auto &v : o["presets"].toArray()) {
        AssetPreset x;
        if (!AssetPreset::fromJson(v.toObject(), x, error))
            return false;
        s.presets.push_back(std::move(x));
    }
    for (const auto &v : o["roadNodes"].toArray()) {
        RoadNode x;
        if (!RoadNode::fromJson(v.toObject(), x, error))
            return false;
        s.roadNodes.push_back(std::move(x));
    }
    for (const auto &v : o["roadEdges"].toArray()) {
        RoadEdge x;
        if (!RoadEdge::fromJson(v.toObject(), x, error))
            return false;
        s.roadEdges.push_back(std::move(x));
    }
    for (const auto &v : o["caveMarkers"].toArray()) {
        CaveMarker x;
        if (!CaveMarker::fromJson(v.toObject(), x, error))
            return false;
        s.caveMarkers.push_back(std::move(x));
    }
    if (o.contains("pcgAssets")) {
        if (!arr("pcgAssets")) {
            error = "PCG资产库无效";
            return false;
        }
        QSet<QString> ids;
        for (const auto &value : o["pcgAssets"].toArray()) {
            PcgAsset asset;
            if (!PcgAsset::fromJson(value.toObject(), asset, error))
                return false;
            if (value.toObject()["version"].toInt() == 1 &&
                !indexGraph(s, value.toObject(), asset.volume.graphId, error))
                return false;
            if (ids.contains(asset.id)) {
                error = "PCG资产ID重复";
                return false;
            }
            ids.insert(asset.id);
            s.pcgAssets.push_back(std::move(asset));
        }
    }
    return validateCreation(s, error);
}

QJsonObject PcgAsset::toJson() const {
    return {{"type", "pcg-volume"}, {"version", 2}, {"id", id}, {"name", name}, {"volume", volume.toJson()}};
}
bool PcgAsset::fromJson(const QJsonObject &o, PcgAsset &out, QString &error) {
    const int version = o["version"].toInt();
    if (o["type"] != "pcg-volume" || (version != 1 && version != 2) || !text(o, "id", out.id) ||
        !text(o, "name", out.name, false) ||
        !PcgVolume::fromJson(o["volume"].toObject(), out.volume, error) || !out.volume.sourceId.isEmpty()) {
        error = "PCG范围资产无效";
        return false;
    }
    return true;
}
bool importStructureJson(const QString &path, StructureAsset &asset, QString &error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        error = f.errorString();
        return false;
    }
    QJsonParseError p;
    auto d = QJsonDocument::fromJson(f.readAll(), &p);
    if (p.error != QJsonParseError::NoError || !d.isObject()) {
        error = "结构 JSON 无效；Sponge/WorldEdit NBT 导入将在资产解析器中继续扩展";
        return false;
    }
    if (!StructureAsset::fromJson(d.object(), asset, error))
        return false;
    asset.sourcePath = QFileInfo(path).absoluteFilePath();
    asset.format = "worldbench-structure-json";
    return true;
}

CaveAnalysis analyzeCave(const std::vector<uint8_t> &solid, int width, int height, int depth,
                         const std::vector<QVector3D> &portals, const std::vector<QVector3D> &interiorSeeds) {
    StructureAsset asset;
    asset.id = "analysis";
    asset.width = width;
    asset.height = height;
    asset.depth = depth;
    if (width < 1 || height < 1 || depth < 1 || solid.size() != size_t(width) * height * depth)
        return {};
    for (int y = 0; y < height; ++y)
        for (int z = 0; z < depth; ++z)
            for (int x = 0; x < width; ++x)
                if (solid[(y * depth + z) * width + x])
                    asset.blocks.push_back({x, y, z, "minecraft:stone"});
    std::vector<CaveMarker> markers;
    for (auto p : portals) {
        CaveMarker m;
        m.assetId = asset.id;
        m.kind = "portal";
        m.position = m.end = p;
        markers.push_back(m);
    }
    for (auto p : interiorSeeds) {
        CaveMarker m;
        m.assetId = asset.id;
        m.kind = "interior";
        m.position = m.end = p;
        markers.push_back(m);
    }
    return analyzeAssetCave(asset, markers);
}

std::vector<QVector3D> buildRoadPolyline(const std::vector<RoadNode> &nodes, const RoadEdge &edge,
                                         float spacing) {
    auto a = std::find_if(nodes.begin(), nodes.end(), [&](const auto &n) { return n.id == edge.from; });
    auto b = std::find_if(nodes.begin(), nodes.end(), [&](const auto &n) { return n.id == edge.to; });
    if (a == nodes.end() || b == nodes.end())
        return {};
    const QVector3D delta = b->position - a->position;
    const float length = delta.length();
    const int count = std::max(2, int(std::ceil(length / std::max(.25f, spacing))) + 1);
    std::vector<QVector3D> out;
    out.reserve(count);
    for (int i = 0; i < count; ++i)
        out.push_back(a->position + delta * (float(i) / (count - 1)));
    return out;
}
} // namespace wb
