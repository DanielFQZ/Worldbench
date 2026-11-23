#include "Editor.h"
#include "Viewport.h"
#include <QBuffer>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>
#include <type_traits>
namespace {
QString fresh(const char *prefix) {
    return QString(prefix) + "-" + QUuid::createUuid().toString(QUuid::Id128);
}
QVariantList rows(const QJsonArray &a) {
    return a.toVariantList();
}
QJsonArray xyz(float x, float y, float z) {
    return {x, y, z};
}
QJsonValue remapReferences(const QJsonValue &value, const QMap<QString, QString> &ids) {
    if (value.isArray()) {
        QJsonArray out;
        for (auto v : value.toArray())
            out.append(remapReferences(v, ids));
        return out;
    }
    if (value.isObject()) {
        auto o = value.toObject();
        for (auto i = o.begin(); i != o.end(); ++i) {
            if ((i.key() == "assetId" || i.key() == "id") && i.value().isString() &&
                ids.contains(i.value().toString()))
                i.value() = ids[i.value().toString()];
            else
                i.value() = remapReferences(i.value(), ids);
        }
        return o;
    }
    return value;
}
} // namespace
QVariantMap Editor::creationState() const {
    QJsonObject data;
    QJsonArray graphs, volumes, presets, nodes, edges, markers;
    for (auto &v : m_world.creation.graphs)
        graphs.append(v.toJson());
    for (auto &v : m_world.creation.volumes)
        volumes.append(v.toJson());
    for (auto &v : m_world.creation.presets)
        presets.append(v.toJson());
    for (auto &v : m_world.creation.roadNodes)
        nodes.append(v.toJson());
    for (auto &v : m_world.creation.roadEdges)
        edges.append(v.toJson());
    for (auto &v : m_world.creation.caveMarkers)
        markers.append(v.toJson());
    data = {{"graphs", graphs},   {"volumes", volumes}, {"presets", presets},
            {"roadNodes", nodes}, {"roadEdges", edges}, {"caveMarkers", markers}};
    QJsonArray assets;
    for (auto &a : m_world.creation.assets) {
        auto o = a.toJson(false);
        o["blockCount"] = int(a.blocks.size());
        assets.append(o);
    }
    data["assets"] = assets;
    QJsonArray pcgAssets;
    for (const auto &a : m_world.creation.pcgAssets)
        pcgAssets.append(QJsonObject{
            {"id", a.id}, {"name", a.name}, {"graph", a.graph.toJson()}, {"volume", a.volume.toJson()}});
    data["pcgAssets"] = pcgAssets;
    data["generatedCells"] = m_world.generated ? int(m_world.generated->cells.size()) : 0;
    data["diagnostics"] =
        m_world.generated ? QJsonArray::fromStringList(m_world.generated->diagnostics) : QJsonArray{};
    data["scatterCount"] = m_world.generated ? int(m_world.generated->scatterPoints.size()) : 0;
    return data.toVariantMap();
}
QVariantMap Editor::creationDetails() const {
    auto data = creationState();
    for (auto key : {"volumes", "presets", "roadNodes", "roadEdges"})
        for (auto value : data[key].toList()) {
            auto row = value.toMap();
            if (row["id"].toString() == m_selectedNode) {
                row["kind"] = key;
                row["valid"] = true;
                return row;
            }
        }
    return {{"valid", false}};
}
QVariantList Editor::pcgGraphs() const {
    return creationState()["graphs"].toList();
}
QVariantList Editor::pcgVolumes() const {
    return creationState()["volumes"].toList();
}
QVariantList Editor::structureAssets() const {
    return creationState()["assets"].toList();
}
QVariantList Editor::libraryAssets() const {
    auto rows = structureAssets();
    for (const auto &a : m_world.creation.pcgAssets) {
        const auto size = a.volume.max - a.volume.min;
        rows.append(QVariantMap{{"id", a.id},
                                {"name", a.name},
                                {"kind", "pcg"},
                                {"width", size.x()},
                                {"height", size.y()},
                                {"depth", size.z()}});
    }
    return rows;
}
bool Editor::storePcgAsset(const QString &id) {
    wb::PcgAsset asset;
    auto next = m_world.creation;
    auto volume = std::find_if(next.volumes.begin(), next.volumes.end(), [&](auto &v) { return v.id == id; });
    if (volume == next.volumes.end())
        return false;
    auto graph = std::find_if(next.graphs.begin(), next.graphs.end(),
                              [&](auto &g) { return g.id == volume->graphId; });
    if (graph == next.graphs.end())
        return false;
    asset.id = fresh("pcg-asset");
    asset.name = volume->name;
    asset.volume = *volume;
    asset.graph = *graph;
    asset.volume.sourceId.clear();
    asset.volume.enabled = asset.volume.visible = true;
    const int centerX = int((volume->min.x() + volume->max.x()) * .5f),
              centerZ = int((volume->min.z() + volume->max.z()) * .5f);
    const auto anchor =
        QVector3D(float(centerX), float(m_world.height(centerX, centerZ) + 1), float(centerZ));
    asset.volume.min -= anchor;
    asset.volume.max -= anchor;
    const auto json = QJsonDocument(graph->toJson()).toJson(QJsonDocument::Compact);
    QSet<QString> references;
    for (const auto &a : next.assets)
        if (json.contains(('"' + a.id + '"').toUtf8())) {
            asset.assets.push_back(a);
            references.insert(a.id);
        }
    for (const auto &m : next.caveMarkers)
        if (references.contains(m.assetId))
            asset.markers.push_back(m);
    next.pcgAssets.push_back(asset);
    return commitCreation(std::move(next), "PCG范围加入资产库");
}
QVariantList Editor::assetPresets() const {
    return creationState()["presets"].toList();
}
QVariantList Editor::roadNodes() const {
    return creationState()["roadNodes"].toList();
}
QVariantList Editor::roadEdges() const {
    return creationState()["roadEdges"].toList();
}
QVariantList Editor::caveMarkers() const {
    return creationState()["caveMarkers"].toList();
}
QVariantMap Editor::graphData(const QString &id) const {
    for (auto &g : m_world.creation.graphs)
        if (g.id == id)
            return g.toJson().toVariantMap();
    return {};
}
QVariantMap Editor::assetData(const QString &id) const {
    for (auto &a : m_world.creation.assets)
        if (a.id == id) {
            auto data = a.toJson(false);
            QJsonArray blocks;
            size_t step = std::max(size_t(1), (a.blocks.size() + 8191) / 8192);
            for (size_t i = 0; i < a.blocks.size(); i += step) {
                auto &b = a.blocks[i];
                blocks.append(QJsonArray{b.x, b.y, b.z, b.state});
            }
            data["resourceRevision"] = double(m_resourceRevision);
            data["previewBlocks"] = blocks;
            data["blockCount"] = int(a.blocks.size());
            return data.toVariantMap();
        }
    return {};
}
QVariantMap Editor::caveAnalysis(const QString &id) const {
    for (auto &a : m_world.creation.assets)
        if (a.id == id) {
            auto r = wb::analyzeAssetCave(a, m_world.creation.caveMarkers);
            int inside = 0, portals = 0;
            for (auto label : r.labels) {
                inside += label == 1;
                portals += label == 2;
            }
            QVariantList issues;
            for (auto &s : r.diagnostics)
                issues << s;
            return {{"valid", r.valid},
                    {"interior", inside},
                    {"portals", portals},
                    {"chambers", r.chambers},
                    {"unconfirmedChambers", r.unconfirmedChambers},
                    {"diagnostics", issues}};
        }
    return {{"valid", false}, {"diagnostics", QStringList{"资产不存在"}}};
}
bool Editor::creationEdit(const QString &action, const QVariantMap &data) {
    if (assetBusy() || m_creationStart) {
        emit failure("请等待当前创作操作完成");
        return false;
    }
    auto next = m_world.creation;
    auto d = QJsonObject::fromVariantMap(data);
    QString error;
    if (action == "replace") {
        if (!wb::loadCreation(d, next, error)) {
            emit failure(error);
            return false;
        }
    } else if (action == "graph") {
        auto it = std::find_if(next.graphs.begin(), next.graphs.end(),
                               [&](auto &g) { return g.id == d["id"].toString(); });
        if (it == next.graphs.end()) {
            emit failure("图不存在");
            return false;
        }
        wb::PcgGraph g;
        if (!wb::PcgGraph::fromJson(d, g, error)) {
            emit failure(error);
            return false;
        }
        *it = std::move(g);
    } else if (action == "add" || action == "patch" || action == "remove") {
        const QString kind = d["kind"].toString(), id = d["id"].toString();
        const auto changes = d["changes"].toObject();
        auto edit = [&]<class T>(std::vector<T> &items) {
            auto it = std::find_if(items.begin(), items.end(), [&](auto &v) { return v.id == id; });
            if (action != "add" && it == items.end()) {
                error = "对象不存在";
                return false;
            }
            if (action == "remove") {
                items.erase(it);
                return true;
            }
            QJsonObject obj;
            wb::SharedValues<wb::StructureBlock> shared;
            if (action != "add") {
                if constexpr (std::is_same_v<T, wb::StructureAsset>) {
                    obj = it->toJson(false);
                    shared = it->blocks;
                } else
                    obj = it->toJson();
            }
            for (auto i = changes.begin(); i != changes.end(); ++i)
                if (i.key() != "id" || action == "add")
                    obj[i.key()] = i.value();
            if (action == "add" && !obj.contains("id"))
                obj["id"] = fresh("object");
            T value;
            if (!T::fromJson(obj, value, error))
                return false;
            if constexpr (std::is_same_v<T, wb::StructureAsset>)
                if (action != "add" && !changes.contains("blocks"))
                    value.blocks = shared;
            if (action == "add")
                items.push_back(std::move(value));
            else
                *it = std::move(value);
            return true;
        };
        bool ok = false;
        if (kind == "graphs")
            ok = edit(next.graphs);
        else if (kind == "volumes")
            ok = edit(next.volumes);
        else if (kind == "assets")
            ok = edit(next.assets);
        else if (kind == "presets")
            ok = edit(next.presets);
        else if (kind == "roadNodes")
            ok = edit(next.roadNodes);
        else if (kind == "roadEdges")
            ok = edit(next.roadEdges);
        else if (kind == "caveMarkers")
            ok = edit(next.caveMarkers);
        else if (kind == "pcgAssets")
            ok = edit(next.pcgAssets);
        else
            error = "对象类型无效";
        if (!ok) {
            emit failure(error);
            return false;
        }
        if (action == "remove") {
            if (kind == "graphs")
                std::erase_if(next.volumes, [&](auto &v) { return v.graphId == id; });
            if (kind == "roadNodes")
                std::erase_if(next.roadEdges, [&](auto &e) { return e.from == id || e.to == id; });
            if (kind == "assets") {
                std::erase_if(next.presets, [&](auto &v) { return v.assetId == id; });
                std::erase_if(next.caveMarkers, [&](auto &v) { return v.assetId == id; });
            }
        }
    } else {
        emit failure("未知创作命令");
        return false;
    }
    return commitCreation(std::move(next), "编辑创作对象");
}
bool Editor::createPcgGraph(const QString &name) {
    auto next = m_world.creation;
    wb::PcgGraph graph;
    graph.id = fresh("pcg");
    graph.name = name.isEmpty() ? "地表 PCG 图" : name.left(128);
    graph.nodes = {{"constant", "constant", {{"value", 1}, {"uiX", 40}, {"uiY", 80}}},
                   {"surface",
                    "output_surface",
                    {{"block", "minecraft:stone"},
                     {"under", "minecraft:dirt"},
                     {"depth", 3},
                     {"uiX", 350},
                     {"uiY", 80}}}};
    graph.connections = {{"constant", "value", "surface", "value"}};
    next.graphs.push_back(graph);
    return commitCreation(std::move(next), "新增PCG图");
}
bool Editor::addPcgVolume(const QString &graphId) {
    auto next = m_world.creation;
    if (next.graphs.empty())
        return false;
    wb::PcgVolume v;
    v.id = fresh("pcg-volume");
    v.graphId = graphId.isEmpty() ? next.graphs.front().id : graphId;
    v.name = "PCG范围 " + QString::number(next.volumes.size() + 1);
    const int h = m_world.height(0, 0);
    v.min = {-32, float(h - 32), -32};
    v.max = {32, float(h + 32), 32};
    next.volumes.push_back(v);
    if (!commitCreation(std::move(next), "新增PCG范围"))
        return false;
    setSelectedNode(v.id);
    return true;
}
bool Editor::connectPcgNodes(const QString &graphId, const QString &fromNode, const QString &fromPort,
                             const QString &toNode, const QString &toPort) {
    auto g = QJsonObject::fromVariantMap(graphData(graphId));
    if (g.isEmpty())
        return false;
    auto c = g["connections"].toArray();
    for (int i = c.size() - 1; i >= 0; --i)
        if (c[i].toObject()["toNode"].toString() == toNode && c[i].toObject()["toPort"].toString() == toPort)
            c.removeAt(i);
    c.append(
        QJsonObject{{"fromNode", fromNode}, {"fromPort", fromPort}, {"toNode", toNode}, {"toPort", toPort}});
    g["connections"] = c;
    return creationEdit("graph", g.toVariantMap());
}
bool Editor::importStructureAsset(const QUrl &url) {
    if (!url.isLocalFile())
        return false;
    wb::StructureAsset asset;
    QString error;
    if (QFileInfo(url.toLocalFile()).suffix().compare("wbasset", Qt::CaseInsensitive) == 0) {
        QFile file(url.toLocalFile());
        if (file.size() > 64 * 1024 * 1024 || !file.open(QIODevice::ReadOnly)) {
            emit failure("资产不可读或超过64MiB");
            return false;
        }
        const auto data = QJsonDocument::fromJson(file.readAll()).object();
        if (data["type"] == "pcg-volume") {
            wb::PcgAsset pcg;
            if (!wb::PcgAsset::fromJson(data, pcg, error)) {
                emit failure(error);
                return false;
            }
            pcg.id = fresh("pcg-asset");
            auto next = m_world.creation;
            next.pcgAssets.push_back(pcg);
            return commitCreation(std::move(next), "导入PCG范围资产");
        }
    }
    if (!wb::importStructure(url.toLocalFile(), asset, error)) {
        emit failure(error);
        return false;
    }
    const QString oldId = asset.id;
    asset.id = fresh("asset");
    auto next = m_world.creation;
    if (QFileInfo(url.toLocalFile()).suffix().compare("wbasset", Qt::CaseInsensitive) == 0) {
        QFile f(url.toLocalFile());
        if (f.open(QIODevice::ReadOnly)) {
            auto object = QJsonDocument::fromJson(f.readAll()).object();
            auto preset = object["instance"].toObject();
            if (!preset.isEmpty()) {
                wb::AssetPreset p;
                if (!wb::AssetPreset::fromJson(preset, p, error)) {
                    emit failure(error);
                    return false;
                }
                asset.defaultRotation = p.rotation;
                asset.defaultScale = p.scale;
            }
            for (auto value : object["caveMarkers"].toArray()) {
                wb::CaveMarker m;
                if (!wb::CaveMarker::fromJson(value.toObject(), m, error)) {
                    emit failure(error);
                    return false;
                }
                m.id = fresh("cave-marker");
                m.assetId = asset.id;
                next.caveMarkers.push_back(m);
            }
        }
    }
    next.assets.push_back(asset);
    return commitCreation(std::move(next), "导入结构资产");
}
bool Editor::saveAssetPreset(const QString &id, const QUrl &url) {
    return creationFile("save_asset", id, url);
}
bool Editor::placeAssetInstance(const QString &id, int x, int y, int z) {
    auto next = m_world.creation;
    for (const auto &asset : m_world.creation.pcgAssets)
        if (asset.id == id) {
            QMap<QString, QString> ids;
            for (auto a : asset.assets) {
                const auto old = a.id;
                a.id = fresh("asset");
                ids[old] = a.id;
                next.assets.push_back(a);
            }
            for (auto marker : asset.markers) {
                marker.id = fresh("cave-marker");
                marker.assetId = ids.value(marker.assetId);
                next.caveMarkers.push_back(marker);
            }
            auto graph = asset.graph;
            graph.id = fresh("pcg");
            for (auto &node : graph.nodes)
                node.parameters = remapReferences(node.parameters, ids).toObject();
            next.graphs.push_back(graph);
            auto volume = asset.volume;
            volume.id = fresh("pcg-volume");
            volume.graphId = graph.id;
            volume.min += QVector3D(x, y, z);
            volume.max += QVector3D(x, y, z);
            next.volumes.push_back(volume);
            if (!commitCreation(std::move(next), "放置PCG范围资产"))
                return false;
            setSelectedNode(volume.id);
            return true;
        }
    wb::AssetPreset p;
    p.id = fresh("asset-instance");
    p.assetId = id;
    p.name = "资产实例 " + QString::number(next.presets.size() + 1);
    p.position = {float(x), float(y), float(z)};
    for (auto &a : next.assets)
        if (a.id == id) {
            p.rotation = a.defaultRotation;
            p.scale = a.defaultScale;
        }
    next.presets.push_back(p);
    if (!commitCreation(std::move(next), "放置资产实例"))
        return false;
    setSelectedNode(p.id);
    return true;
}
bool Editor::addRoadNode(int x, int y, int z) {
    auto next = m_world.creation;
    wb::RoadNode n{fresh("road-node"),
                   "道路节点 " + QString::number(next.roadNodes.size() + 1),
                   {float(x), float(y), float(z)},
                   true};
    next.roadNodes.push_back(n);
    if (!commitCreation(std::move(next), "新增道路节点"))
        return false;
    setSelectedNode(n.id);
    return true;
}
bool Editor::connectRoadNodes(const QString &from, const QString &to, int width) {
    auto next = m_world.creation;
    next.roadEdges.push_back({fresh("road-edge"), from, to, width, "minecraft:gravel", false, false, true});
    return commitCreation(std::move(next), "连接道路节点");
}
bool Editor::addCaveMarker(const QString &id, const QString &kind, int x, int y, int z) {
    auto next = m_world.creation;
    wb::CaveMarker m;
    m.id = fresh("cave-marker");
    m.assetId = id;
    m.kind = kind;
    m.position = m.end = {float(x), float(y), float(z)};
    next.caveMarkers.push_back(m);
    return commitCreation(std::move(next), "新增洞穴标记");
}
QVariantMap Editor::evaluatePcg(const QString &id, double x, double z) const {
    for (auto &g : m_world.creation.graphs)
        if (g.id == id) {
            QString error;
            auto value = g.evaluate(x, z, m_world.height(int(x), int(z)), 0, m_world.seed, &error);
            return {{"ok", error.isEmpty()}, {"value", value}, {"error", error}};
        }
    return {{"ok", false}, {"error", "图不存在"}};
}
bool Editor::placeAt(int x, int y, int z) {
    if (m_roadPlacement)
        return addRoadNode(x, y, z);
    if (m_placementAsset.isEmpty())
        return false;
    bool ok = placeAssetInstance(m_placementAsset, x, y + 1, z);
    if (ok) {
        m_placementAsset.clear();
        emit changed();
    }
    return ok;
}
bool Editor::creationFile(const QString &action, const QString &id, const QUrl &url) {
    if (!url.isLocalFile())
        return false;
    QString path = url.toLocalFile();
    if (action == "load_asset")
        return importStructureAsset(url);
    if (action == "load_graph") {
        QFile f(path);
        if (f.size() > 64 * 1024 * 1024 || !f.open(QIODevice::ReadOnly)) {
            emit failure("图文件不可读或超过64MiB");
            return false;
        }
        auto doc = QJsonDocument::fromJson(f.readAll());
        wb::PcgGraph g;
        QString error;
        const auto bundle = doc.object();
        if (!wb::PcgGraph::fromJson(bundle.contains("graph") ? bundle["graph"].toObject() : bundle, g,
                                    error)) {
            emit failure(error);
            return false;
        }
        g.id = fresh("pcg");
        auto next = m_world.creation;
        QMap<QString, QString> remap;
        for (auto value : bundle["assets"].toArray()) {
            wb::StructureAsset asset;
            if (!wb::StructureAsset::fromJson(value.toObject(), asset, error)) {
                emit failure(error);
                return false;
            }
            const auto old = asset.id;
            asset.id = fresh("asset");
            remap[old] = asset.id;
            next.assets.push_back(asset);
        }
        for (auto &node : g.nodes)
            if (node.type == "output_scatter") {
                auto assetId = node.parameters["assetId"].toString();
                if (remap.contains(assetId))
                    node.parameters["assetId"] = remap[assetId];
                auto weighted = node.parameters["assets"].toArray();
                for (int i = 0; i < weighted.size(); ++i) {
                    auto entry = weighted[i].toObject();
                    auto old = entry["id"].toString();
                    if (remap.contains(old))
                        entry["id"] = remap[old];
                    weighted[i] = entry;
                }
                node.parameters["assets"] = weighted;
            }
        for (auto value : bundle["caveMarkers"].toArray()) {
            wb::CaveMarker marker;
            if (!wb::CaveMarker::fromJson(value.toObject(), marker, error) ||
                !remap.contains(marker.assetId)) {
                emit failure("图资产中的洞穴标记引用无效");
                return false;
            }
            marker.id = fresh("cave-marker");
            marker.assetId = remap[marker.assetId];
            next.caveMarkers.push_back(marker);
        }
        next.graphs.push_back(g);
        return commitCreation(std::move(next), "导入PCG图");
    }
    QJsonObject data;
    if (action == "save_graph") {
        const auto graph = QJsonObject::fromVariantMap(graphData(id));
        if (!graph.isEmpty()) {
            QSet<QString> references;
            for (auto value : graph["nodes"].toArray()) {
                auto node = value.toObject();
                if (node["type"] != "output_scatter")
                    continue;
                auto p = node["parameters"].toObject();
                references.insert(p["assetId"].toString());
                for (auto asset : p["assets"].toArray())
                    references.insert(asset.toObject()["id"].toString());
            }
            QJsonArray assets, markers;
            for (auto &asset : m_world.creation.assets)
                if (references.contains(asset.id))
                    assets.append(asset.toJson());
            for (auto &marker : m_world.creation.caveMarkers)
                if (references.contains(marker.assetId))
                    markers.append(marker.toJson());
            data = {{"version", 1}, {"graph", graph}, {"assets", assets}, {"caveMarkers", markers}};
        }
    }
    if (action == "save_asset") {
        for (const auto &asset : m_world.creation.pcgAssets)
            if (asset.id == id)
                data = asset.toJson();
        QString assetId = id;
        QJsonObject instance;
        for (auto &p : m_world.creation.presets)
            if (p.id == id) {
                assetId = p.assetId;
                instance = p.toJson();
            }
        for (auto &a : m_world.creation.assets)
            if (a.id == assetId)
                data = QJsonObject{{"version", 1}, {"asset", a.toJson()}, {"instance", instance}};
        QJsonArray markers;
        for (auto &m : m_world.creation.caveMarkers)
            if (m.assetId == assetId)
                markers.append(m.toJson());
        if (data.contains("asset"))
            data["caveMarkers"] = markers;
    }
    if (data.isEmpty()) {
        emit failure("没有可保存的图或资产");
        return false;
    }
    QSaveFile file(path);
    auto bytes = QJsonDocument(data).toJson(QJsonDocument::Compact);
    if (bytes.size() > 64 * 1024 * 1024) {
        emit failure("便携资产/图超过64MiB交换预算，请缩小资产");
        return false;
    }
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        emit failure(file.errorString());
        return false;
    }
    return true;
}

void Editor::beginCreationTransform() {
    if (!m_creationStart && !commandBusy())
        m_creationStart = std::make_shared<const wb::CreationState>(m_world.creation);
}
void Editor::previewCreationTransform(const QVariantMap &changes) {
    if (!m_creationStart)
        return;
    for (const auto &value : changes)
        if (!std::isfinite(value.toDouble()))
            return;
    auto next = *m_creationStart;
    bool found = false;
    const QStringList positions{"x", "y", "z"}, scales{"scaleX", "scaleY", "scaleZ"};
    for (auto &v : next.volumes)
        if (v.id == m_selectedNode) {
            found = true;
            const auto center = (v.min + v.max) * .5f;
            for (int axis = 0; axis < 3; ++axis) {
                if (changes.contains(scales[axis])) {
                    const float factor = float(std::clamp(changes[scales[axis]].toDouble(), .01, 16.));
                    const float half = std::max(.5f, (v.max[axis] - v.min[axis]) * .5f * factor);
                    v.min[axis] = center[axis] - half;
                    v.max[axis] = center[axis] + half;
                }
                if (changes.contains(positions[axis])) {
                    const float offset = float(std::round(changes[positions[axis]].toDouble()));
                    v.min[axis] += offset;
                    v.max[axis] += offset;
                }
            }
        }
    for (auto &p : next.presets)
        if (p.id == m_selectedNode) {
            found = true;
            for (int axis = 0; axis < 3; ++axis) {
                p.position[axis] += float(std::round(changes.value(positions[axis], 0).toDouble()));
                if (changes.contains(scales[axis])) {
                    const float sign = p.scale[axis] < 0 ? -1.f : 1.f;
                    p.scale[axis] = sign * float(std::clamp(int(std::round(std::abs(p.scale[axis]) *
                                                                           changes[scales[axis]].toDouble())),
                                                            1, 8));
                }
            }
            p.rotation.setY(p.rotation.y() +
                            float(std::round(changes.value("rotationY", 0).toDouble() / 90) * 90));
        }
    for (auto &n : next.roadNodes)
        if (n.id == m_selectedNode) {
            found = true;
            for (int axis = 0; axis < 3; ++axis)
                n.position[axis] += float(std::round(changes.value(positions[axis], 0).toDouble()));
        }
    if (!found || next == m_world.creation)
        return;
    // A gesture updates transforms only. The renderer moves the cached mesh;
    // authoritative PCG/voxel generation is committed once on release.
    m_world.creation = std::move(next);
    emit changed();
}
void Editor::endCreationTransform(bool cancel) {
    if (!m_creationStart)
        return;
    auto before = m_creationStart;
    m_creationStart.reset();
    if (*before == m_world.creation)
        return;
    const auto next = m_world.creation;
    m_world.creation = *before;
    if (!cancel)
        commitCreation(next, "变换创作实例");
    emit changed();
}

// The snapshot is computed on a worker. A guarded event loop keeps the native
// synchronous GUI/MCP command contract while allowing progress and cancellation.
// All mutating entry points see assetBusy; revision is checked again at publish.
bool Editor::prepareCreation(wb::World &candidate, QString &error, bool cancellable) {
    if (candidate.creation.volumes.empty() && candidate.creation.presets.empty() &&
        candidate.creation.roadEdges.empty())
        return candidate.rebuildCreation(error);
    const auto revision = m_revision;
    auto control = std::make_shared<CreationControl>();
    m_creationControl = control;
    m_creationBusy = true;
    m_creationCancellable = cancellable;
    emit changed();
    QFutureWatcher<bool> watcher;
    QEventLoop loop;
    QTimer timer;
    timer.setInterval(80);
    connect(&timer, &QTimer::timeout, this, &Editor::changed);
    connect(&watcher, &QFutureWatcherBase::finished, &loop, &QEventLoop::quit);
    watcher.setFuture(QtConcurrent::run([&candidate, &error, control] {
        return candidate.rebuildCreation(error, [control](int percent, const QString &) {
            control->progress = percent;
            return !control->cancelled.load();
        });
    }));
    timer.start();
    loop.exec();
    timer.stop();
    bool ok = watcher.result();
    m_creationBusy = false;
    m_creationCancellable = false;
    m_creationControl.reset();
    emit changed();
    if (revision != m_revision) {
        error = "生成期间工程已变化，结果已丢弃";
        return false;
    }
    return ok;
}
QString Editor::pcgPreview(const QString &graphId, const QString &nodeId, const QString &volumeId) const {
    const wb::PcgGraph *graph = nullptr;
    for (auto &g : m_world.creation.graphs)
        if (g.id == graphId)
            graph = &g;
    if (!graph)
        return {};
    wb::PcgPlan plan;
    QString error;
    if (!wb::compileGraph(*graph, plan, error))
        return {};
    int index = -1;
    for (int i = 0; i < int(plan.instructions.size()); ++i)
        if (plan.instructions[i].id == nodeId)
            index = i;
    if (index < 0)
        return {};
    wb::PcgVolume volume;
    volume.min = {-32, 0, -32};
    volume.max = {32, 320, 32};
    for (auto &v : m_world.creation.volumes)
        if ((volumeId.isEmpty() && v.graphId == graphId) || v.id == volumeId) {
            volume = v;
            break;
        }
    // Use the same integer column fields as the authoritative generator.
    auto without = m_world.heights;
    const bool bound = !volume.sourceId.isEmpty();
    bool sourceEnabled = !bound;
    if (bound)
        for (auto &source : without.instances)
            if (source.id == volume.sourceId) {
                sourceEnabled = source.enabled;
                source.enabled = false;
            }
    auto terrainHeight = [&](int x, int z) {
        x = std::clamp(x, m_world.originX, m_world.originX + m_world.width - 1);
        z = std::clamp(z, m_world.originZ, m_world.originZ + m_world.depth - 1);
        return int(m_world.heights[m_world.column(x, z)]);
    };
    QImage image(128, 128, QImage::Format_RGB32);
    image.fill(Qt::black);
    for (int z = 0; z < 128; ++z)
        for (int x = 0; x < 128; ++x) {
            int wx = int(std::floor(volume.min.x() + (x + .5) * (volume.max.x() - volume.min.x()) / 128)),
                wz = int(std::floor(volume.min.z() + (z + .5) * (volume.max.z() - volume.min.z()) / 128));
            if (!m_world.contains(wx, 0, wz) || !sourceEnabled || !volume.visible || !volume.enabled)
                continue;
            int h = m_world.height(wx, wz), column = m_world.column(wx, wz);
            if (h < volume.min.y() || h >= volume.max.y())
                continue;
            double mask =
                volume.transition > 0
                    ? std::clamp(
                          std::min({wx + .5 - double(volume.min.x()), double(volume.max.x()) - wx - .5,
                                    wz + .5 - double(volume.min.z()), double(volume.max.z()) - wz - .5}) /
                              volume.transition,
                          0., 1.)
                    : 1.;
            mask = mask * mask * (3 - 2 * mask);
            if (bound && volume.contributionOnly)
                mask *=
                    std::clamp(double(std::abs(int(m_world.heights[column]) - int(without[column]))), 0., 1.);
            double slope =
                std::atan(std::hypot((terrainHeight(wx + 1, wz) - terrainHeight(wx - 1, wz)) * .5,
                                     (terrainHeight(wx, wz + 1) - terrainHeight(wx, wz - 1)) * .5)) *
                180 / 3.141592653589793;
            double v = plan.evaluateValues(
                {double(wx), double(wz), double(h), double(m_world.heights.base(column)), slope,
                 wx - volume.min.x(), wz - volume.min.z(), mask, volume.seed, 1, &volume.overrides})[index];
            int c = int(std::clamp(v, 0., 1.) * 255);
            image.setPixel(x, z, qRgb(c, c, c));
        }
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return "data:image/png;base64," + QString::fromLatin1(bytes.toBase64());
}
QVariantMap Editor::assetSlice(const QString &id, int y, bool classified) const {
    for (auto &a : m_world.creation.assets)
        if (a.id == id) {
            QVariantList cells;
            for (auto &b : a.blocks)
                if (b.y == y)
                    cells << QVariant(QVariantList{b.x, b.z});
            QVariantList labels;
            if (classified) {
                auto r = wb::analyzeAssetCave(a, m_world.creation.caveMarkers);
                if (y >= 0 && y < a.height)
                    for (int z = 0; z < a.depth; ++z)
                        for (int x = 0; x < a.width; ++x) {
                            int label = r.labels.empty() ? 0 : r.labels[(y * a.depth + z) * a.width + x];
                            if (label)
                                labels << QVariant(QVariantList{x, z, label});
                        }
            }
            return {{"blocks", cells}, {"labels", labels}};
        }
    return {};
}

Editor *Editor::assetInspector() {
    if (!m_assetInspector) {
        m_assetInspector = std::make_unique<Editor>();
        m_assetInspector->m_inspectionOnly = true;
        m_assetInspector->m_tool = -1;
    }
    return m_assetInspector.get();
}
bool Editor::showAssetPreview(const QString &id) {
    auto found = std::find_if(m_world.creation.assets.begin(), m_world.creation.assets.end(),
                              [&](auto &a) { return a.id == id; });
    if (found == m_world.creation.assets.end())
        return false;
    auto *inspector = assetInspector();
    if (inspector->m_world.assets == m_world.assets && !inspector->m_world.creation.assets.empty() &&
        inspector->m_world.creation.assets.front().blocks == found->blocks && inspector->m_inspectionId == id)
        return true;
    auto asset = *found;
    asset.placement = "place";
    asset.groundBlend = 0;
    asset.entities.clear();
    asset.pivot = {float(asset.width / 2), 0, float(asset.depth / 2)};
    wb::WorldOptions options;
    options.width = std::max(32, ((asset.width + 31) / 32) * 32);
    options.depth = std::max(32, ((asset.depth + 31) / 32) * 32);
    options.flat = true;
    options.baseHeight = -63;
    options.seaLevel = -64;
    auto world = wb::World::create(options);
    world.setAssets(m_world.assets);
    world.creation.assets.push_back(asset);
    wb::AssetPreset p;
    p.id = "asset-preview";
    p.assetId = asset.id;
    p.position = {0, -63, 0};
    world.creation.presets.push_back(p);
    QString error;
    if (!world.rebuildCreation(error)) {
        emit failure(error);
        return false;
    }
    inspector->m_world = std::move(world);
    inspector->m_inspectionId = id;
    ++inspector->m_revision;
    ++inspector->m_epoch;
    inspector->dirtyAll();
    emit inspector->changed();
    emit inspector->worldReset();
    return true;
}
