#include "Editor.h"
#include "Viewport.h"
#include <QFileInfo>
#include <QUuid>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>
int Editor::selectedTerrainIndex() const {
    const auto &instances = m_world.heights.instances;
    for (size_t i = 0; i < instances.size(); ++i)
        if (instances[i].id == m_selectedNode)
            return int(i);
    return -1;
}
void Editor::setSelectedNode(const QString &id) {
    if (regionBusy() || m_creationBusy)
        return;
    if (id == m_selectedNode)
        return;
    bool creationNode = false;
    for (auto value : sceneNodes())
        if (value.toMap()["id"].toString() == id)
            creationNode = true;
    if (!id.isEmpty() && !creationNode && !id.startsWith("environment") &&
        m_world.heights.layerIndex(id) < 0 &&
        std::none_of(m_world.heights.instances.begin(), m_world.heights.instances.end(),
                     [&](const auto &i) { return i.id == id; }))
        return;
    endTerrainEdit();
    endStroke();
    endEnvironmentEdit();
    m_selectedNode = id;
    if (m_world.heights.layerIndex(id) >= 0 && m_world.heights.activeLayer != id) {
        m_world.heights.activeLayer = id;
        ++m_revision; // A pending implicit-layer MCP sculpt must not silently change its target.
    }
    emit changed();
}
QVariantMap Editor::terrainDetails() const {
    const int index = selectedTerrainIndex();
    if (index < 0)
        return {{"valid", false}};
    const auto &i = m_world.heights.instances[index];
    return {{"valid", true},
            {"id", i.id},
            {"name", i.name},
            {"source", i.sourceName},
            {"mask", i.maskName},
            {"pixelsX", i.image->width},
            {"pixelsZ", i.image->depth},
            {"y", i.y},
            {"rotationX", i.rotationX},
            {"rotationY", i.rotationY},
            {"rotationZ", i.rotationZ},
            {"scaleX", i.scaleX},
            {"scaleY", i.scaleY},
            {"scaleZ", i.scaleZ},
            {"centerX", i.x + (i.width - 1) / 2},
            {"centerZ", i.z + (i.depth - 1) / 2},
            {"x", i.x},
            {"z", i.z},
            {"width", i.width},
            {"depth", i.depth},
            {"low", i.low},
            {"high", i.high},
            {"blend", i.blend},
            {"opacity", i.opacity},
            {"feather", i.feather},
            {"blendSmoothness", i.blendSmoothness},
            {"blendEdge", i.blendEdge},
            {"enabled", i.enabled},
            {"canUp", index + 1 < int(m_world.heights.instances.size())},
            {"canDown", index > 0}};
}
void Editor::commitTerrain(std::vector<wb::TerrainInstance> next, const QString &label) {
    if (assetBusy())
        return;
    QString error;
    auto creationNames = m_world.creation;
    if (!prepareLibraryNames(creationNames, next, false, error) || !wb::validateTerrain(next, error)) {
        emit failure(error);
        return;
    }
    if (next == m_world.heights.instances)
        return;
    endStroke();
    wb::Command command;
    command.label = label;
    command.terrainBefore =
        std::make_shared<const std::vector<wb::TerrainInstance>>(m_world.heights.instances);
    command.terrainAfter = std::make_shared<const std::vector<wb::TerrainInstance>>(std::move(next));
    for (const auto &old : *command.terrainBefore)
        if (creationNames.assetFolders.contains(old.id) &&
            std::none_of(command.terrainAfter->begin(), command.terrainAfter->end(),
                         [&](const auto &t) { return t.id == old.id; }))
            creationNames.assetFolders.remove(old.id);
    if (creationNames.assetFolders != m_world.creation.assetFolders) {
        command.creationBefore = std::make_shared<const wb::CreationState>(m_world.creation);
        command.creationAfter = std::make_shared<const wb::CreationState>(std::move(creationNames));
    }
    m_world.apply(command, true);
    ++m_revision;
    if (!dirtyCommand(command))
        return;
    // Do not cancel every pointer event: let the current complete preview
    // publish, then coalesce pending changes into the next snapshot.
    if (!m_terrainStart)
        pushCommand(std::move(command));
    m_status = label + " · Ctrl+Z 撤销";
    emit changed();
}
wb::TerrainInstance Editor::importedTerrain(const wb::World &world, const QString &fileName,
                                            std::shared_ptr<const wb::HeightmapData> image) {
    wb::TerrainInstance i;
    i.id = "heightmap-" + QUuid::createUuid().toString(QUuid::Id128);
    i.name = QFileInfo(fileName).completeBaseName().left(128);
    if (i.name.isEmpty())
        i.name = "高度图地貌";
    i.sourceName = fileName;
    i.image = image;
    i.width = std::min(world.width, image->width);
    i.depth = std::min(world.depth, image->depth);
    i.x = world.originX + (world.width - i.width) / 2;
    i.z = world.originZ + (world.depth - i.depth) / 2;
    i.low = 0;
    i.high = 64;
    i.blend = 1;
    return i;
}
void Editor::initializeTerrainImporter() {
    connect(&m_heightmapWatcher, &QFutureWatcherBase::finished, this, [this] {
        m_heightmapLoading = false;
        const auto result = m_heightmapWatcher.future().takeResult();
        if (!result.image) {
            m_status = "高度图导入失败，工程未改动";
            emit failure(result.error);
            emit changed();
            return;
        }
        auto next = m_world.heights.instances;
        QString selected;
        if (result.mask) {
            auto it = std::find_if(next.begin(), next.end(),
                                   [&](const auto &i) { return i.id == result.targetId; });
            if (it == next.end()) {
                emit failure("目标地貌实例已不存在");
                emit changed();
                return;
            }
            it->mask = result.image;
            it->maskName = result.fileName;
            selected = it->id;
        } else {
            auto i = importedTerrain(m_world, result.fileName, result.image);
            selected = i.id;
            next.push_back(std::move(i));
        }
        QString error;
        if (!wb::validateTerrain(next, error)) {
            emit failure(error);
            m_status = error;
            emit changed();
            return;
        }
        commitTerrain(std::move(next), result.mask ? "已应用地貌蒙版" : "已导入高度图实例");
        setSelectedNode(selected);
    });
}
void Editor::importHeightmap(const QUrl &url, bool mask) {
    if (assetBusy() || exportBusy())
        return;
    if (!url.isLocalFile() || (mask && selectedTerrainIndex() < 0)) {
        emit failure(mask ? "请先选择一个高度图实例" : "请选择本地灰度 PNG");
        return;
    }
    endTerrainEdit();
    endStroke();
    m_heightmapLoading = true;
    m_status = mask ? "正在后台读取地貌蒙版…" : "正在后台读取高度图…";
    const auto path = url.toLocalFile(), targetId = m_selectedNode;
    const auto sources = m_world.heights.instances;
    m_heightmapWatcher.setFuture(QtConcurrent::run([path, targetId, mask, sources] {
        HeightmapResult r;
        r.fileName = QFileInfo(path).fileName();
        r.targetId = targetId;
        r.mask = mask;
        r.image = wb::loadHeightmap(path, r.error);
        if (r.image)
            for (const auto &instance : sources)
                for (const auto &image : {instance.image, instance.mask})
                    if (image && *image == *r.image) {
                        r.image = image;
                        return r;
                    }
        return r;
    }));
    emit changed();
}
void Editor::setTerrainOption(const QString &key, const QVariant &value) {
    setTerrainOptions({{key, value}});
}
void Editor::setTerrainOptions(const QVariantMap &values) {
    const int index = selectedTerrainIndex();
    if (index < 0 || assetBusy())
        return;
    auto next = m_world.heights.instances;
    auto &i = next[index];
    for (auto it = values.begin(); it != values.end(); ++it) {
        const auto key = it.key();
        const auto value = it.value();
        if (key == "enabled")
            i.enabled = value.toBool();
        else if (key == "name")
            i.name = value.toString().trimmed();
        else {
            bool ok = false;
            double number = value.toDouble(&ok);
            if (!ok || !std::isfinite(number) || number != std::floor(number) || std::abs(number) > 2000000)
                return;
            int n = int(number);
            if (key == "x")
                i.x = n;
            else if (key == "z")
                i.z = n;
            else if (key == "centerX")
                i.x = n - (i.width - 1) / 2;
            else if (key == "centerZ")
                i.z = n - (i.depth - 1) / 2;
            else if (key == "y")
                i.y = n;
            else if (key == "width")
                i.width = n;
            else if (key == "depth")
                i.depth = n;
            else if (key == "low")
                i.low = n;
            else if (key == "high")
                i.high = n;
            else if (key == "blend")
                i.blend = n;
            else if (key == "opacity")
                i.opacity = n;
            else if (key == "feather")
                i.feather = n;
            else if (key == "blendSmoothness")
                i.blendSmoothness = n;
            else if (key == "blendEdge")
                i.blendEdge = n;
            else if (key == "rotationX")
                i.rotationX = n;
            else if (key == "rotationY")
                i.rotationY = n;
            else if (key == "rotationZ")
                i.rotationZ = n;
            else if (key == "scaleX")
                i.scaleX = n;
            else if (key == "scaleY")
                i.scaleY = n;
            else if (key == "scaleZ")
                i.scaleZ = n;
            else
                return;
        }
    }
    commitTerrain(std::move(next), "已调整地貌实例");
}
void Editor::beginTerrainEdit() {
    if (m_terrainStart || assetBusy() || selectedTerrainIndex() < 0)
        return;
    endStroke();
    m_terrainStart = std::make_shared<const std::vector<wb::TerrainInstance>>(m_world.heights.instances);
    // Begin a generation so late preview results cannot cross cancel/release.
    ++m_gesture;
    if (m_meshCancel)
        m_meshCancel->store(true);
    m_interactiveMesh = true;
    m_rejectStaleMeshes = true;
    m_status = "交互编辑 · 正在实时构建完整预览网格，松开后更新高精度地形";
    emit changed();
}
void Editor::endTerrainEdit(bool cancel) {
    if (!m_terrainStart)
        return;
    wb::Command c;
    c.label = "地貌变换";
    c.terrainBefore = m_terrainStart;
    c.terrainAfter = std::make_shared<const std::vector<wb::TerrainInstance>>(m_world.heights.instances);
    m_terrainStart.reset();
    const bool wasInteractive = m_interactiveMesh;
    m_interactiveMesh = false;
    ++m_gesture;
    if (*c.terrainBefore == *c.terrainAfter) {
        m_rejectStaleMeshes = false;
        if (wasInteractive && m_meshCancel)
            m_meshCancel->store(true);
        if (wasInteractive && m_scene->provisional) {
            m_farDirty = true;
            m_meshTimer.start();
        }
        emit changed();
        return;
    }
    if (cancel) {
        m_world.apply(c, false);
        ++m_revision;
        dirtyCommand(c, false);
        emit changed();
    } else
        pushCommand(std::move(c));
    if (wasInteractive) {
        if (m_meshCancel)
            m_meshCancel->store(true);
        // dirtyCommand has already coalesced the changed instance bounds. Do
        // one formal rebuild after release instead of rebuilding every drag
        // sample or forcing the entire world dirty.
        m_farDirty = true;
        if (!m_meshTimer.isActive())
            m_meshTimer.start();
        emit changed();
    }
}
void Editor::commitLayers(std::vector<wb::SculptLayer> next, const QString &label) {
    if (assetBusy())
        return;
    endTerrainEdit();
    endStroke();
    wb::Command c;
    c.label = label;
    c.layersBefore = std::make_shared<const std::vector<wb::SculptLayer>>(m_world.heights.layers);
    c.layersAfter = std::make_shared<const std::vector<wb::SculptLayer>>(std::move(next));
    if (*c.layersBefore == *c.layersAfter)
        return;
    m_world.apply(c, true);
    ++m_revision;
    if (!dirtyCommand(c))
        return;
    pushCommand(std::move(c));
}
void Editor::setNodeVisible(const QString &id, bool visible) {
    if (id.startsWith("environment")) {
        setEnvironment({{id == "environment" ? "enabled" : id.mid(12), visible}});
        return;
    }
    if (assetBusy())
        return;
    endTerrainEdit();
    auto next = m_world.heights.instances;
    for (auto &i : next)
        if (i.id == id) {
            i.enabled = visible;
            commitTerrain(std::move(next), "切换地貌可见性");
            return;
        }
    auto layers = m_world.heights.layers;
    for (auto &l : layers)
        if (l.id == id) {
            l.enabled = visible;
            commitLayers(std::move(layers), "切换地形层");
            return;
        }
    auto creation = m_world.creation;
    for (auto &v : creation.volumes)
        if (v.id == id) {
            v.enabled = visible;
            v.visible = visible;
            commitCreation(std::move(creation), "切换 PCG 范围可见性");
            return;
        }
    for (auto &p : creation.presets)
        if (p.id == id) {
            p.enabled = visible;
            commitCreation(std::move(creation), "切换资产实例可见性");
            return;
        }
    for (auto &n : creation.roadNodes)
        if (n.id == id) {
            n.enabled = visible;
            commitCreation(std::move(creation), "切换道路节点可见性");
            return;
        }
}
void Editor::addSculptLayer() {
    if (m_world.heights.layers.size() >= 32) {
        emit failure("本版最多32个雕刻层");
        return;
    }
    auto layers = m_world.heights.layers;
    wb::SculptLayer l;
    l.id = "sculpt-" + QUuid::createUuid().toString(QUuid::Id128);
    l.name = "地形层" + QString::number(layers.size() + 1);
    layers.push_back(l);
    commitLayers(std::move(layers), "新增地形层");
    setSelectedNode(l.id);
}
void Editor::removeSculptLayer() {
    int n = m_world.heights.layerIndex(m_selectedNode);
    if (n <= 0)
        return;
    auto layers = m_world.heights.layers;
    layers.erase(layers.begin() + n);
    commitLayers(std::move(layers), "移除地形层");
}
void Editor::renameSculptLayer(const QString &name) {
    int n = m_world.heights.layerIndex(m_selectedNode);
    auto trimmed = name.trimmed();
    if (n < 0 || trimmed.isEmpty() || trimmed.size() > 128)
        return;
    auto layers = m_world.heights.layers;
    layers[n].name = trimmed;
    commitLayers(std::move(layers), "重命名地形层");
}
void Editor::setActiveSculptLayer(const QString &id) {
    if (m_world.heights.layerIndex(id) >= 0)
        setSelectedNode(id);
}
void Editor::removeTerrain() {
    const int index = selectedTerrainIndex();
    if (index < 0)
        return;
    auto next = m_world.heights.instances;
    next.erase(next.begin() + index);
    commitTerrain(std::move(next), "已移除地貌实例");
}
void Editor::moveTerrain(int direction) {
    const int index = selectedTerrainIndex(), target = index + (direction > 0 ? 1 : -1);
    auto next = m_world.heights.instances;
    if (index < 0 || target < 0 || target >= int(next.size()))
        return;
    std::swap(next[index], next[target]);
    commitTerrain(std::move(next), "已调整地貌叠放顺序");
}
void Editor::clearTerrainMask() {
    const int index = selectedTerrainIndex();
    if (index < 0)
        return;
    auto next = m_world.heights.instances;
    next[index].mask.reset();
    next[index].maskName.clear();
    commitTerrain(std::move(next), "已移除地貌蒙版");
}
void Editor::focusTerrain() {
    const int index = selectedTerrainIndex();
    auto *viewport = qobject_cast<Viewport *>(m_cameraController.data());
    if (index < 0 || !viewport)
        return;
    const auto &i = m_world.heights.instances[index];
    viewport->jumpTo(i.x + i.width / 2, i.z + i.depth / 2);
}
