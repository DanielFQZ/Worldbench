#include "Viewport.h"
#include "ViewportRenderer.h"
#include <QCoreApplication>
#include <QCursor>
#include <QJsonArray>
#include <QKeySequence>
#include <QSettings>
#include <stdexcept>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#endif
#include <QFile>
#include <QMouseEvent>
#include <QQuickGraphicsConfiguration>
#include <QQuickWindow>
#include <QWheelEvent>
#include <QtConcurrent>
#include <QtMath>
#include <algorithm>
#include <array>
#include <cstring>
#include <rhi/qrhi.h>

namespace {
bool matchesShortcut(const QKeyEvent *event, const QString &sequence) {
    if (sequence.isEmpty())
        return false;
    const auto actual = QKeySequence(event->modifiers() | event->key());
    return QKeySequence::fromString(sequence, QKeySequence::PortableText).matches(actual) ==
           QKeySequence::ExactMatch;
}
} // namespace

Viewport::Viewport(QQuickItem *parent) : QQuickRhiItem(parent) {
    m_textureAnisotropy = QSettings().value("rendering/textureAnisotropy", 8).toInt();
    if (m_textureAnisotropy != 1 && m_textureAnisotropy != 4 && m_textureAnisotropy != 8) m_textureAnisotropy = 8;
    m_blockGrid = QSettings().value("rendering/blockGrid", false).toBool();
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton | Qt::MiddleButton);
    setAcceptHoverEvents(true);
    setFlag(ItemIsFocusScope);
    setKeepMouseGrab(true);
    // Screen-space markers must follow layout changes even with a static camera.
    connect(this, &QQuickItem::widthChanged, this, &Viewport::cameraChanged);
    connect(this, &QQuickItem::heightChanged, this, &Viewport::cameraChanged);
    connect(this, &QQuickItem::widthChanged, this, &Viewport::updateRenderSize);
    connect(this, &QQuickItem::heightChanged, this, &Viewport::updateRenderSize);
    connect(this, &QQuickItem::windowChanged, this, [this] {
        if (window())
            connect(window(), &QWindow::screenChanged, this, &Viewport::updateRenderSize,
                    Qt::UniqueConnection);
        if (window() && !window()->isSceneGraphInitialized()) {
            auto config = window()->graphicsConfiguration();
            config.setTimestamps(true);
            window()->setGraphicsConfiguration(config);
        }
        updateRenderSize();
    });
    connect(&m_ghostWatcher, &QFutureWatcherBase::finished, this, [this] {
        m_ghostMesh = m_ghostWatcher.result();
        update();
    });
    connect(&m_proxyWatcher, &QFutureWatcherBase::finished, this, [this] {
        m_selectionProxy = m_proxyWatcher.result();
        refreshSelectionProxy();
        update();
    });
    m_timer.setInterval(16);
    connect(&m_timer, &QTimer::timeout, this, &Viewport::tick);
    m_elapsed.start();
    m_stampClock.start();
    m_timer.start();
    m_metricsTimer.setInterval(500);
    connect(&m_metricsTimer, &QTimer::timeout, this, &Viewport::metricsChanged);
    m_metricsTimer.start();
    QSettings settings;
    m_snapEnabled = settings.value("input/transformSnap", false).toBool();
    m_rawInput = settings.value("input/raw", true).toBool();
    m_flightInertia = settings.value("input/flightInertia", true).toBool();
    m_sensitivity = std::clamp(settings.value("input/sensitivity", .12).toDouble(), .01, 1.0);
    setSampleCount(settings.value("rendering/antialiasing", true).toBool() ? 4 : 1);
    m_renderScale = std::clamp(settings.value("rendering/scale", 100).toInt(), 50, 100);
    m_shadowQuality = std::clamp(settings.value("rendering/shadowQuality", 2).toInt(), 0, 2);
    QCoreApplication::instance()->installNativeEventFilter(this);
}
void Viewport::updateRenderSize() {
    const double scale = (window() ? window()->devicePixelRatio() : 1.) * m_renderScale / 100.;
    setFixedColorBufferWidth(m_renderScale == 100 ? 0 : std::max(1, int(std::ceil(width() * scale))));
    setFixedColorBufferHeight(m_renderScale == 100 ? 0 : std::max(1, int(std::ceil(height() * scale))));
    update();
}
void Viewport::setRenderScale(int value) {
    value = std::clamp(value, 50, 100);
    if (m_renderScale == value)
        return;
    m_renderScale = value;
    QSettings().setValue("rendering/scale", value);
    updateRenderSize();
    emit cameraChanged();
}
void Viewport::setShadowQuality(int value) {
    value = std::clamp(value, 0, 2);
    if (m_shadowQuality == value)
        return;
    m_shadowQuality = value;
    QSettings().setValue("rendering/shadowQuality", value);
    update();
    emit cameraChanged();
}
void Viewport::setTextureAnisotropy(int value) {
    if ((value != 1 && value != 4 && value != 8) || value == m_textureAnisotropy) return;
    m_textureAnisotropy = value;
    QSettings().setValue("rendering/textureAnisotropy", value);
    emit cameraChanged(); update();
}
void Viewport::setBlockGrid(bool enabled) {
    if (m_blockGrid == enabled) return;
    m_blockGrid = enabled;
    QSettings().setValue("rendering/blockGrid", enabled);
    emit cameraChanged(); update();
}
void Viewport::setAntialiasing(bool enabled) {
    const bool current = sampleCount() > 1;
    if (current == enabled)
        return;
    QSettings settings;
    settings.setValue("rendering/antialiasing", enabled);
    setSampleCount(enabled ? 4 : 1);
    emit cameraChanged();
    update();
}
QQuickRhiItemRenderer *Viewport::createRenderer() {
    return createTerrainRenderer();
}
void Viewport::setEditor(Editor *e) {
    if (m_editor == e)
        return;
    if (m_editor)
        disconnect(m_editor, nullptr, this, nullptr);
    m_editor = e;
    if (e) {
        e->setCameraController(this);
        connect(e, &Editor::sceneChanged, this, [this] {
            if (m_ghostUntilRevision && m_editor->scene()->sourceRevision >= m_ghostUntilRevision)
                clearAssetPreview();
            const auto scene = m_editor->scene();
            if (!scene->provisional && !scene->farTiles.empty() &&
                m_detailRevision != scene->sourceRevision) {
                m_detailRevision = scene->sourceRevision;
                updatePreviewDetail();
            }
            update();
        });
        connect(e, &Editor::worldReset, this, [this] {
            m_detailRevision = ~uint64_t(0);
            clearAssetPreview();
            m_ghostId.clear();
        });
        connect(e, &Editor::catalogChanged, this, [this] {
            clearAssetPreview();
            m_ghostId.clear();
        });
        connect(e, &Editor::placementChanged, this, [this] {
            refreshSelectionProxy();
            update();
            emit cameraChanged();
        });
        connect(e, &Editor::worldReset, this, [this] {
            if (m_editor->world().savedCamera.isEmpty()) resetCamera();
            else applyCamera(m_editor->world().savedCamera);
        });
        if (e->inspectionOnly())
            resetCamera();
    }
    emit editorChanged();
    update();
}
void Viewport::refreshSelectionProxy() {
    if (!m_editor || m_proxyWatcher.isRunning())
        return;
    const auto &world = m_editor->world();
    const wb::StructureAsset *source = nullptr;
    for (const auto &p : world.creation.presets)
        if (p.id == m_editor->selectedNode() && p.enabled)
            for (const auto &a : world.creation.assets)
                if (a.id == p.assetId && a.placement == "carve")
                    source = &a;
    const auto identity = source ? source->blocks.identity() : nullptr;
    if (identity == m_proxyBlocks && world.assets == m_proxyLibrary)
        return;
    m_proxyBlocks = identity;
    m_proxyLibrary = world.assets;
    m_selectionProxy.reset();
    if (source)
        m_proxyWatcher.setFuture(QtConcurrent::run([world, asset = *source] {
            try {
                return wb::buildAssetGhost(world, asset);
            } catch (const std::exception &) {
                return wb::MeshPtr{};
            }
        }));
}
QString Viewport::cameraText() const {
    return QString("X %1   Y %2   Z %3   |   速度 %4")
        .arg(m_camera.x(), 0, 'f', 1)
        .arg(m_camera.y(), 0, 'f', 1)
        .arg(m_camera.z(), 0, 'f', 1)
        .arg(m_speed, 0, 'f', 0);
}
QString Viewport::hitText() const {
    return m_hit.valid
               ? QString("%1 · (%2, %3, %4)%5")
                     .arg(m_editor->world().palette[m_hit.block].label())
                     .arg(m_hit.x)
                     .arg(m_hit.y)
                     .arg(m_hit.z)
                     .arg(!m_editor->previewContains(m_hit.x, m_hit.z) &&
                                  (m_editor->tool() == 0 || m_editor->tool() == 1 || m_editor->tool() == 5)
                              ? " · 远处精确落点 / 范围以实际格数为准"
                              : "")
               : "指向体素以雕刻";
}
QVector3D Viewport::forward() const {
    float y = qDegreesToRadians(m_yaw), p = qDegreesToRadians(m_pitch);
    return {std::sin(y) * std::cos(p), std::sin(p), -std::cos(y) * std::cos(p)};
}
QVector3D Viewport::right() const {
    return QVector3D::crossProduct(forward(), {0, 1, 0}).normalized();
}
QMatrix4x4 Viewport::viewProjection() const {
    QMatrix4x4 v, p;
    v.lookAt(m_camera, m_camera + forward(), {0, 1, 0});
    float nearPlane = std::clamp((m_camera.y() - wb::World::MaxY) * .002f, .15f, 64.f);
    float farPlane = 3000;
    if (m_editor && !m_editor->inspectionOnly()) {
        const auto &w = m_editor->world();
        QVector3D center(float(w.originX + w.width / 2), 128, float(w.originZ + w.depth / 2));
        farPlane = std::max(3000.f, (m_camera - center).length() +
                                        std::hypot(float(w.width), float(w.depth)) + 1024.f);
    }
    p.perspective(60, float(std::max(1., width()) / std::max(1., height())), nearPlane, farPlane);
    return p * v;
}
QString Viewport::pickCreation(QPointF position) const {
    const auto &world = m_editor->world();
    QString selected;
    float limit = std::numeric_limits<float>::max();
    if (m_hit.valid) {
        limit = (m_hit.position - m_camera).length() + .05f;
        if (world.generated) {
            const auto key = world.key(m_hit.x, m_hit.y, m_hit.z);
            const auto found = world.generated->objectCells.get(key);
            if (found && !world.explicitOverride(key))
                selected = world.generated->objects[*found].ownerId;
        }
    }
    const auto view = viewProjection();
    for (const auto &volume : m_editor->renderCreation().volumes) {
        if (!volume.visible)
            continue;
        const auto pose = volume.orientation();
        const float inset = std::min({volume.transition, (volume.max.x() - volume.min.x()) * .475f,
                                      (volume.max.z() - volume.min.z()) * .475f});
        for (float margin : {0.f, inset}) {
            const auto lo = volume.min + QVector3D(margin, 0, margin),
                       hi = volume.max - QVector3D(margin, 0, margin);
            auto corner = [&](int i) {
                return pose.map(
                    QVector3D(i & 1 ? hi.x() : lo.x(), i & 2 ? hi.y() : lo.y(), i & 4 ? hi.z() : lo.z()));
            };
            for (int i = 0; i < 8; ++i)
                for (int bit : {1, 2, 4}) {
                    if (i & bit)
                        continue;
                    auto a = corner(i), b = corner(i | bit);
                    float wa = (view * QVector4D(a, 1)).w(), wb = (view * QVector4D(b, 1)).w();
                    if (wa <= .15f && wb <= .15f)
                        continue;
                    if (wa <= .15f) {
                        a += (b - a) * ((.15f - wa) / (wb - wa));
                        wa = .15f;
                    }
                    if (wb <= .15f) {
                        b += (a - b) * ((.15f - wb) / (wa - wb));
                        wb = .15f;
                    }
                    auto p = projectPoint(a), line = projectPoint(b) - p;
                    const double length2 = QPointF::dotProduct(line, line);
                    if (length2 < 1e-6)
                        continue;
                    const auto t = std::clamp(QPointF::dotProduct(position - p, line) / length2, 0., 1.);
                    if (QLineF(position, p + line * t).length() > 5.)
                        continue;
                    const auto hit = a + (b - a) * float(t * wa / ((1 - t) * wb + t * wa));
                    const float distance = (hit - m_camera).length();
                    if (distance < limit) {
                        limit = distance;
                        selected = volume.id;
                    }
                }
            if (inset <= 0)
                break;
        }
    }
    return selected;
}
void Viewport::pick(QPointF pos) {
    if (m_sculpting && pos == m_pointer && m_editor &&
        (m_editor->tool() == 0 || m_editor->tool() == 1 || m_editor->tool() == 5) && m_hit.valid) {
        if (m_hit.block != wb::Water) {
            const auto &w = m_editor->world();
            m_hit.y = w.heights[w.column(m_hit.x, m_hit.z)];
            m_hit.position = {m_hit.x + .5f, float(m_hit.y + 1), m_hit.z + .5f};
            m_hit.block = w.terrainBlock(m_hit.x, m_hit.y, m_hit.z, m_hit.y);
            m_hit.normal = {0, 1, 0};
        }
        update();
        emit cameraChanged();
        return;
    }
    m_pointer = pos;
    if (!m_editor || width() < 1 || height() < 1)
        return;
    const auto inv = viewProjection().inverted();
    QVector4D farPoint =
        inv * QVector4D(float(pos.x() / width() * 2 - 1), float(1 - pos.y() / height() * 2), 1, 1);
    const auto &world = m_editor->world();
    const QVector3D center(float(world.originX + world.width / 2), 128,
                           float(world.originZ + world.depth / 2));
    const float distance =
        (m_camera - center).length() + std::hypot(float(world.width), float(world.depth)) + 1024.f;
    // Surface tools and selection query authoritative voxels over the entire
    // map. Volume/material tools still need visible exact model geometry.
    m_hit = world.raycast(m_camera, farPoint.toVector3DAffine() - m_camera, distance,
                          m_editor->tool() >= 0 && m_editor->tool() <= 5);
    if (m_hit.valid && m_editor->tool() >= 2 && m_editor->tool() <= 4 &&
        !m_editor->previewContains(m_hit.x, m_hit.z))
        m_hit = {};
    update();
    emit cameraChanged();
}
void Viewport::changedCamera() {
    m_pitch = std::clamp(m_pitch, -89.f, 89.f);
    updatePreviewDetail();
    pick(m_pointer);
    update();
}
void Viewport::updatePreviewDetail() {
    if (m_editor && !m_editor->inspectionOnly()) {
        const auto &w = m_editor->world();
        const float px = std::clamp(m_camera.x(), float(w.originX), float(w.originX + w.width - 1));
        const float pz = std::clamp(m_camera.z(), float(w.originZ), float(w.originZ + w.depth - 1));
        QVector3D nearest(px, float(w.height(int(px), int(pz)) + 1), pz);
        float best = (nearest - m_camera).lengthSquared();
        const float range = m_editor->exactRadiusChunks() * 16.f + 32.f;
        if (best >= range * range) {
            auto consider = [&](QVector3D lo, QVector3D hi) {
                QVector3D q;
                for (int i = 0; i < 3; ++i)
                    q[i] = std::clamp(m_camera[i], lo[i], hi[i]);
                float d = (q - m_camera).lengthSquared();
                if (d < best) {
                    best = d;
                    nearest = q;
                }
            };
            const auto scene = m_editor->scene();
            for (const auto &tile : scene->farTiles) {
                if (!tile.mesh ||
                    !tile.area.adjusted(-range, -range, range, range).contains(int(px), int(pz)))
                    continue;
                const int stride = (tile.area.width() + 15) / 16;
                for (size_t i = 0; i < tile.mesh->terrainRanges.size(); ++i) {
                    auto heights = tile.mesh->terrainRanges[i];
                    float x = tile.area.x() + int(i % stride) * 16, z = tile.area.y() + int(i / stride) * 16;
                    consider({x, float(heights.low), z}, {x + 16, float(heights.high), z + 16});
                }
            }
            for (const auto &obj : scene->objects) {
                QVector3D offset;
                if (obj.id == obj.ownerId)
                    for (const auto &placement : scene->creation.presets)
                        if (placement.id == obj.id) {
                            offset = placement.position - obj.placement.position;
                            break;
                        }
                consider(obj.mesh->lower + offset, obj.mesh->upper + offset);
            }
            for (int z = wb::floorDiv(int(std::floor(m_camera.z() - range)), 16);
                 z <= wb::floorDiv(int(std::floor(m_camera.z() + range)), 16); ++z)
                for (int x = wb::floorDiv(int(std::floor(m_camera.x() - range)), 16);
                     x <= wb::floorDiv(int(std::floor(m_camera.x() + range)), 16); ++x) {
                    auto chunk = scene->editedBounds.find({x, z});
                    if (chunk != scene->editedBounds.end())
                        for (const auto &[section, box] : chunk->second)
                            consider(box.first, box.second);
                }
        }
        m_editor->setExactPreviewEnabled(best < range * range);
        m_editor->setPreviewCenter(int(nearest.x()), int(nearest.z()));
    }
}
void Viewport::resetCamera() {
    stopInput();
    const auto &w = m_editor->world();
    if (m_editor->inspectionOnly() && !w.heights.instances.empty()) {
        m_pivot = {};
        m_camera = {70, 54, 80};
        const auto direction = (-m_camera).normalized();
        m_yaw = qRadiansToDegrees(std::atan2(direction.x(), -direction.z()));
        m_pitch = qRadiansToDegrees(std::asin(direction.y()));
        m_speed = 30;
        changedCamera();
        return;
    }
    if (m_editor->inspectionOnly() && !w.creation.assets.empty()) {
        const auto &asset = w.creation.assets.front();
        m_pivot = {0, -63.f + asset.height * .5f, 0};
        const float extent = std::max({asset.width, asset.depth, asset.height}) * 1.8f;
        m_camera = m_pivot + QVector3D(extent * .65f, extent * .42f, extent * .8f);
        const auto direction = (m_pivot - m_camera).normalized();
        m_yaw = qRadiansToDegrees(std::atan2(direction.x(), -direction.z()));
        m_pitch = qRadiansToDegrees(std::asin(direction.y()));
        m_speed = std::max(3.f, extent * .3f);
        changedCamera();
        return;
    }
    if (w.width > 256 || w.depth > 256) {
        frameWorld();
        return;
    }
    m_pivot = {float(w.originX + w.width / 2),
               float(w.height(w.originX + w.width / 2, w.originZ + w.depth / 2)),
               float(w.originZ + w.depth / 2)};
    const float extent = float(std::min(128, std::max(w.width, w.depth)));
    m_camera = m_pivot + QVector3D(extent * .625f, 45, extent * .78125f);
    const auto f = (m_pivot - m_camera).normalized();
    m_yaw = qRadiansToDegrees(std::atan2(f.x(), -f.z()));
    m_pitch = qRadiansToDegrees(std::asin(f.y()));
    m_speed = 35;
    changedCamera();
}
QString Viewport::renderText() const {
    return QString("CPU提交 %1 ms · 上传 %2 MiB · 网格/颜色 %3 MiB · %4 次绘制 · 光影目标 %5 MiB · GPU %6 ms "
                   "· %7×%8 · 资产LOD %9")
        .arg(m_metrics->submitMicros.load() / 1000., 0, 'f', 2)
        .arg(m_metrics->uploadKiB.load() / 1024., 0, 'f', 1)
        .arg(m_metrics->residentKiB.load() / 1024., 0, 'f', 1)
        .arg(m_metrics->draws.load())
        .arg(m_metrics->effectKiB.load() / 1024., 0, 'f', 1)
        .arg(m_metrics->gpuMicros.load() / 1000., 0, 'f', 2)
        .arg(m_metrics->pixelWidth.load())
        .arg(m_metrics->pixelHeight.load())
        .arg(m_metrics->objectLods.load());
}
void Viewport::frameWorld() {
    if (!m_editor)
        return;
    stopInput();
    const auto &w = m_editor->world();
    m_pivot = {float(w.originX + w.width / 2),
               float(w.height(w.originX + w.width / 2, w.originZ + w.depth / 2)),
               float(w.originZ + w.depth / 2)};
    const float aspect = float(std::max(1., width()) / std::max(1., height()));
    const float radius = std::hypot(float(w.width), float(w.depth)) * .5f + 192;
    const float halfAngle = std::atan(std::tan(qDegreesToRadians(30.f)) * std::min(1.f, aspect));
    const float distance = radius / std::sin(halfAngle) * 1.22f;
    m_camera = m_pivot + QVector3D(.3f, .85f, .45f).normalized() * distance;
    const auto f = (m_pivot - m_camera).normalized();
    m_yaw = qRadiansToDegrees(std::atan2(f.x(), -f.z()));
    m_pitch = qRadiansToDegrees(std::asin(f.y()));
    m_speed = std::clamp(float(std::max(w.width, w.depth)) * .25f, 35.f, 8000.f);
    changedCamera();
}
void Viewport::mouseDoubleClickEvent(QMouseEvent *e) {
    if (m_editor && m_editor->tool() == -1 && e->button() == Qt::LeftButton &&
        e->modifiers() == Qt::NoModifier && width() > 0 && height() > 0) {
        auto p = viewProjection().inverted() * QVector4D(float(e->position().x() / width() * 2 - 1),
                                                         float(1 - e->position().y() / height() * 2), 1, 1);
        const auto hit = m_editor->world().raycast(m_camera, p.toVector3DAffine() - m_camera, 100000);
        if (hit.valid &&
            (!m_editor->previewContains(hit.x, hit.z) || (hit.position - m_camera).length() > 300)) {
            jumpTo(hit.x, hit.z);
            e->accept();
            return;
        }
    }
    QQuickRhiItem::mouseDoubleClickEvent(e);
}
void Viewport::focusTarget() {
    m_velocity = {};
    if (!m_editor)
        return;
    const auto &w = m_editor->world();
    QVector3D center, span;
    const auto id = m_editor->selectedNode();
    for (const auto &p : w.creation.presets)
        if (p.id == id)
            for (const auto &a : w.creation.assets)
                if (a.id == p.assetId) {
                    const auto transform = wb::placementMatrix(p);
                    span = {float(a.width), float(a.height), float(a.depth)};
                    center = transform.map(span * .5f - a.pivot);
                    span *= p.scale;
                }
    for (const auto &v : w.creation.volumes)
        if (v.id == id) {
            center = (v.min + v.max) * .5f;
            span = v.max - v.min;
        }
    for (const auto &n : w.creation.roadNodes)
        if (n.id == id) {
            center = n.position;
            span = {6, 6, 6};
        }
    for (const auto &t : w.heights.instances)
        if (t.id == id) {
            const auto bounds = t.bounds();
            center = {float(bounds.center().x()),
                      float(w.height(int(bounds.center().x()), int(bounds.center().y()))),
                      float(bounds.center().y())};
            span = {float(bounds.width()), float((t.high - t.low) * t.scaleY / 100.), float(bounds.height())};
        }
    if (!span.isNull()) {
        const float aspect = float(width() / std::max(1., height()));
        const float halfAngle = std::atan(.57735f * std::min(1.f, aspect));
        m_pivot = center;
        m_camera = center - forward() * std::max(4.f, span.length() * .55f / std::sin(halfAngle));
        changedCamera();
        return;
    }
    m_pivot = m_hit.valid ? m_hit.position : QVector3D(0, 20, 0);
    m_camera = m_pivot - forward() * (m_hit.valid ? 24.f : 150.f);
    changedCamera();
}
void Viewport::mousePressEvent(QMouseEvent *e) {
    m_toolCycleActive = false;
    forceActiveFocus();
    if (e->buttons() == Qt::LeftButton && !(e->modifiers() & ~Qt::ControlModifier) &&
        pressGizmo(e->position())) {
        e->accept();
        return;
    }
    const bool wasNavigating =
        m_altNav || m_buttons.testFlag(Qt::RightButton) || m_buttons.testFlag(Qt::MiddleButton);
    m_buttons = e->buttons();
    m_altNav = e->modifiers().testFlag(Qt::AltModifier);
    m_reverse = e->modifiers().testFlag(Qt::ShiftModifier);
    m_pointer = e->position();
    pick(m_pointer);
    const bool nav = m_altNav || m_buttons.testFlag(Qt::RightButton) || m_buttons.testFlag(Qt::MiddleButton);
    if (nav) {
        if (m_sculpting && m_editor)
            m_editor->endStroke();
        m_sculpting = false;
        if (!wasNavigating && m_editor) {
            // Freeze a pivot on the view axis for this gesture. Picking at the cursor
            // forces the camera to realign on the first orbit delta and causes a snap.
            const auto direction = forward();
            const auto centerHit = m_editor->world().raycast(m_camera, direction, 100000);
            float depth = centerHit.valid ? QVector3D::dotProduct(centerHit.position - m_camera, direction)
                                          : QVector3D::dotProduct(m_pivot - m_camera, direction);
            if (depth <= .01f)
                depth = 100.f;
            m_pivot = m_camera + direction * depth;
        }
        m_anchorGlobal = e->globalPosition();
        setCursor(QCursor(Qt::BlankCursor));
        beginRawInput();
    } else if (e->button() == Qt::LeftButton && m_editor && !m_editor->placementAsset().isEmpty() &&
               m_hit.valid) {
        m_editor->placeAt(m_hit.x, m_hit.y, m_hit.z);
    } else if (e->button() == Qt::LeftButton && m_editor && m_editor->tool() == 6 && m_hit.valid) {
        m_selecting = true;
        m_selectionStart = {m_hit.x, m_hit.z};
        m_editor->setSelection(m_hit.x, m_hit.z, m_hit.x, m_hit.z);
    } else if (e->button() == Qt::LeftButton && m_editor && m_hit.valid && m_editor->tool() >= 0 &&
               m_editor->tool() <= 5 && !m_editor->assetBusy()) {
        m_sculpting = true;
        m_editor->beginStroke();
        m_editor->stamp(m_hit, m_reverse);
        m_stampClock.restart();
    } else if (e->button() == Qt::LeftButton && m_editor && m_editor->tool() == -1 &&
               !m_editor->inspectionOnly() && m_editor->placementAsset().isEmpty()) {
        m_editor->setSelectedNode(pickCreation(e->position()));
    }
    e->accept();
}
void Viewport::mouseReleaseEvent(QMouseEvent *e) {
    if (m_gizmoDragging && e->button() == Qt::LeftButton) {
        finishGizmo();
        e->accept();
        return;
    }
    m_buttons = e->buttons();
    if (e->button() == Qt::LeftButton)
        m_selecting = false;
    if (e->button() == Qt::LeftButton && m_sculpting) {
        if (m_editor)
            m_editor->endStroke();
        m_sculpting = false;
    }
    if (m_buttons == Qt::NoButton) {
        m_velocity = {};
        endRawInput();
        unsetCursor();
        m_keys.clear();
        m_altNav = false;
    }
    e->accept();
}
void Viewport::mouseMoveEvent(QMouseEvent *e) {
    if (m_gizmoDragging) {
        moveGizmo(e->position());
        e->accept();
        return;
    }
    m_reverse = e->modifiers().testFlag(Qt::ShiftModifier);
    const bool nav = m_altNav || m_buttons.testFlag(Qt::RightButton) || m_buttons.testFlag(Qt::MiddleButton);
    if (nav) {
        if (m_rawActive) {
            e->accept();
            return;
        }
        const QPointF delta = e->globalPosition() - m_anchorGlobal;
        m_anchorGlobal = e->globalPosition();
        navigate(float(delta.x()), float(delta.y()));
    } else {
        pick(e->position());
        if (m_selecting && m_hit.valid && m_editor)
            m_editor->setSelection(m_selectionStart.x(), m_selectionStart.y(), m_hit.x, m_hit.z);
        if (m_sculpting && m_editor && m_stampClock.elapsed() > 60) {
            m_editor->stamp(m_hit, m_reverse);
            m_stampClock.restart();
        }
    }
    e->accept();
}
void Viewport::wheelEvent(QWheelEvent *e) {
    const float steps = e->angleDelta().y() / 120.f;
    if (m_buttons.testFlag(Qt::RightButton))
        m_speed = std::clamp(m_speed * std::pow(1.25f, steps), 1.f, 8000.f);
    else
        m_camera += forward() * steps * std::max(1.f, m_speed * .15f);
    changedCamera();
    e->accept();
}
void Viewport::hoverMoveEvent(QHoverEvent *e) {
    if (m_buttons == Qt::NoButton) {
        m_gizmoHover = hitGizmo(e->position(), gizmoOverlay());
        pick(e->position());
    }
}
void Viewport::hoverLeaveEvent(QHoverEvent *) {
    if (m_buttons == Qt::NoButton) {
        m_hit = {};
        m_gizmoHover = -1;
        update();
        emit cameraChanged();
    }
}
void Viewport::keyPressEvent(QKeyEvent *e) {
    if (!m_editor) {
        e->ignore();
        return;
    }
    const bool cycleTool = matchesShortcut(e, m_editor->shortcut("toolCycle"));
    if (!cycleTool)
        m_toolCycleActive = false;
    if (m_buttons == Qt::NoButton && !m_editor->commandBusy() && !m_editor->exportBusy()) {
        if (cycleTool) {
            if (!e->isAutoRepeat() || !m_toolCycleClock.isValid() || m_toolCycleClock.elapsed() >= 220) {
                const bool consecutive = m_toolCycleActive && m_toolCycleLast == m_editor->tool() &&
                                         m_toolCycleClock.elapsed() < 1500;
                m_editor->setTool(consecutive ? (m_editor->tool() == 6 ? -1 : m_editor->tool() + 1) : 0);
                m_toolCycleLast = m_editor->tool();
                m_toolCycleActive = true;
                m_toolCycleClock.restart();
            }
            e->accept();
            return;
        }
        const std::array<QString, 7> actions{"toolNavigate", "toolFlatten", "toolCarve", "toolFill",
                                             "toolPaint",    "toolSmooth",  "toolSelect"};
        for (int i = 0; i < int(actions.size()); ++i) {
            if (matchesShortcut(e, m_editor->shortcut(actions[i]))) {
                m_editor->setTool(i == 0 ? -1 : i);
                e->accept();
                return;
            }
        }
    }
    if (matchesShortcut(e, m_editor->shortcut("deleteTerrain")) && !m_editor->commandBusy() &&
        !m_editor->exportBusy() &&
        (m_editor->terrainDetails()["valid"].toBool() || m_editor->creationDetails()["valid"].toBool())) {
        auto detail = m_editor->creationDetails();
        if (detail["valid"].toBool())
            m_editor->creationEdit("remove", {{"kind", detail["kind"]}, {"id", detail["id"]}});
        else
            m_editor->removeTerrain();
        e->accept();
        return;
    }
    if (matchesShortcut(e, m_editor->shortcut("cancelSelection"))) {
        if (!m_gizmoDragging && m_editor->placementAsset().isEmpty() && !m_sculpting && !m_altNav)
            m_editor->setSelectedNode({});
        m_editor->beginPlacement("");
        m_editor->endCreationTransform(true);
        if (m_gizmoDragging)
            finishGizmo(true);
        stopInput();
        e->accept();
        return;
    }
    if (matchesShortcut(e, m_editor->shortcut("focus"))) {
        focusTarget();
        e->accept();
        return;
    }
    if (matchesShortcut(e, m_editor->shortcut("brushSmaller"))) {
        m_editor->setRadius(m_editor->radius() - 1);
        e->accept();
        return;
    }
    if (matchesShortcut(e, m_editor->shortcut("brushLarger"))) {
        m_editor->setRadius(m_editor->radius() + 1);
        e->accept();
        return;
    }
    if (m_buttons.testFlag(Qt::RightButton) && !m_altNav) {
        m_keys.insert(e->key());
        e->accept();
        return;
    }
    if ((m_editor->terrainDetails()["valid"].toBool() || m_editor->creationDetails()["valid"].toBool()) &&
        (matchesShortcut(e, m_editor->shortcut("translateGizmo")) ||
         matchesShortcut(e, m_editor->shortcut("rotateGizmo")) ||
         matchesShortcut(e, m_editor->shortcut("scaleGizmo")))) {
        setGizmoMode(matchesShortcut(e, m_editor->shortcut("translateGizmo")) ? 0
                     : matchesShortcut(e, m_editor->shortcut("rotateGizmo"))  ? 1
                                                                              : 2);
        m_editor->setTool(-1);
        e->accept();
        return;
    }
    if (e->key() == Qt::Key_Shift)
        m_reverse = true;
    e->ignore();
}
void Viewport::keyReleaseEvent(QKeyEvent *e) {
    if (!e->isAutoRepeat())
        m_keys.remove(e->key());
    if (e->key() == Qt::Key_Shift)
        m_reverse = false;
    e->accept();
}
void Viewport::stopInput() {
    finishGizmo();
    m_velocity = {};
    endRawInput();
    m_keys.clear();
    m_buttons = Qt::NoButton;
    m_altNav = false;
    m_selecting = false;
    if (m_sculpting && m_editor)
        m_editor->endStroke();
    m_sculpting = false;
    unsetCursor();
}
void Viewport::focusOutEvent(QFocusEvent *e) {
    m_toolCycleActive = false;
    stopInput();
    QQuickRhiItem::focusOutEvent(e);
}
void Viewport::mouseUngrabEvent() {
    stopInput();
}
void Viewport::tick() {
    const float dt = std::min(.05f, float(m_elapsed.restart()) * .001f);
    // Animate shader normals without dirtying the world or rebuilding geometry.
    if (m_editor && isVisible() && window() && window()->isVisible() &&
        window()->visibility() != QWindow::Minimized && m_editor->scene()->waterVertices &&
        m_metrics->visibleWater && m_editor->world().environment.water &&
        m_editor->world().environment.waterWaves && m_editor->world().environment.waveStrength > 0)
        update();
    if (!hasActiveFocus() || !window() || !window()->isActive()) {
        if (m_buttons != Qt::NoButton)
            stopInput();
        return;
    }
    if (m_buttons.testFlag(Qt::RightButton) && !m_altNav) {
        QVector3D move;
        if (m_keys.contains(Qt::Key_W))
            move += forward();
        if (m_keys.contains(Qt::Key_S))
            move -= forward();
        if (m_keys.contains(Qt::Key_D))
            move += right();
        if (m_keys.contains(Qt::Key_A))
            move -= right();
        if (m_keys.contains(Qt::Key_E))
            move += QVector3D(0, 1, 0);
        if (m_keys.contains(Qt::Key_Q))
            move -= QVector3D(0, 1, 0);
        const auto target = move.normalized() * m_speed * (m_keys.contains(Qt::Key_Shift) ? 3.f : 1.f);
        QVector3D displacement;
        if (m_flightInertia) {
            // Exact integration of exponential acceleration/braking, independent of tick rate.
            const float tau = move.isNull() ? .09f : .12f;
            const float decay = std::exp(-dt / tau);
            displacement = target * dt + (m_velocity - target) * (tau * (1 - decay));
            m_velocity = target + (m_velocity - target) * decay;
            if (move.isNull() && m_velocity.lengthSquared() < .0001f)
                m_velocity = {};
        } else {
            m_velocity = target;
            displacement = target * dt;
        }
        if (!displacement.isNull()) {
            m_camera += displacement;
            changedCamera();
        }
    } else
        m_velocity = {};
    if (m_sculpting && m_editor && m_stampClock.elapsed() > 100) {
        pick(m_pointer);
        m_editor->stamp(m_hit, m_reverse);
        m_stampClock.restart();
    }
}

Viewport::~Viewport() {
    m_ghostWatcher.waitForFinished();
    endRawInput();
    QCoreApplication::instance()->removeNativeEventFilter(this);
}
void Viewport::setRawInput(bool value) {
    stopInput();
    m_rawInput = value;
    QSettings().setValue("input/raw", value);
    emit cameraChanged();
}
void Viewport::setSensitivity(double value) {
    if (!std::isfinite(value))
        return;
    m_sensitivity = std::clamp(value, .01, 1.0);
    QSettings().setValue("input/sensitivity", m_sensitivity);
    emit cameraChanged();
}
void Viewport::setFlightInertia(bool value) {
    m_flightInertia = value;
    m_velocity = {};
    QSettings().setValue("input/flightInertia", value);
    emit cameraChanged();
}
void Viewport::beginRawInput() {
#ifdef Q_OS_WIN
    if (!m_rawInput || m_rawActive || !window())
        return;
    RAWINPUTDEVICE device{1, 2, 0, reinterpret_cast<HWND>(window()->winId())};
    if (!RegisterRawInputDevices(&device, 1, sizeof(device)))
        return;
    POINT p;
    GetCursorPos(&p);
    RECT clip{p.x, p.y, p.x + 1, p.y + 1};
    if (!ClipCursor(&clip)) {
        RAWINPUTDEVICE remove{1, 2, RIDEV_REMOVE, nullptr};
        RegisterRawInputDevices(&remove, 1, sizeof(remove));
        return;
    }
    m_rawActive = true;
    emit cameraChanged();
#endif
}
void Viewport::endRawInput() {
#ifdef Q_OS_WIN
    if (m_rawActive) {
        ClipCursor(nullptr);
        RAWINPUTDEVICE device{1, 2, RIDEV_REMOVE, nullptr};
        RegisterRawInputDevices(&device, 1, sizeof(device));
    }
#endif
    if (m_rawActive) {
        m_rawActive = false;
        emit cameraChanged();
    }
}
bool Viewport::nativeEventFilter(const QByteArray &, void *message, qintptr *) {
#ifdef Q_OS_WIN
    auto *msg = static_cast<MSG *>(message);
    if (m_rawActive && msg->message == WM_INPUT && hasActiveFocus() && window() && window()->isActive()) {
        RAWINPUT raw{};
        UINT size = sizeof(raw);
        if (GetRawInputData(reinterpret_cast<HRAWINPUT>(msg->lParam), RID_INPUT, &raw, &size,
                            sizeof(RAWINPUTHEADER)) != UINT(-1) &&
            raw.header.dwType == RIM_TYPEMOUSE) {
            if (!(raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) {
                ++m_rawPackets;
                navigate(float(raw.data.mouse.lLastX), float(raw.data.mouse.lLastY));
            }
        }
    }
#else
    Q_UNUSED(message);
#endif
    return false;
}
void Viewport::navigate(float dx, float dy) {
    if (!std::isfinite(dx) || !std::isfinite(dy) || (dx == 0 && dy == 0))
        return;
    const float radius = std::max(.01f, (m_camera - m_pivot).length());
    const float distance = std::max(5.f, radius), scale = float(m_sensitivity / .2);
    if (m_buttons.testFlag(Qt::MiddleButton) ||
        (m_buttons.testFlag(Qt::LeftButton) && m_buttons.testFlag(Qt::RightButton))) {
        QVector3D move = (-right() * dx + QVector3D(0, 1, 0) * dy) * (distance * .0015f * scale);
        m_camera += move;
        m_pivot += move;
    } else if (m_altNav && m_buttons.testFlag(Qt::RightButton))
        m_camera += forward() * (dx - dy) * distance * .005f * scale;
    else {
        m_yaw += dx * float(m_sensitivity);
        m_pitch = std::clamp(m_pitch - dy * float(m_sensitivity), -89.f, 89.f);
        if (m_altNav)
            m_camera = m_pivot - forward() * radius;
    }
    changedCamera();
}
QJsonObject Viewport::cameraState() const {
    return {{"position", QJsonArray{m_camera.x(), m_camera.y(), m_camera.z()}},
            {"yaw", double(m_yaw)}, {"pitch", double(m_pitch)}};
}
void Viewport::applyCamera(const QJsonObject &a) {
    if (a.isEmpty())
        return;
    auto camera = m_camera;
    float yaw = m_yaw, pitch = m_pitch;
    auto number = [](const QJsonValue &v, double lo, double hi) {
        if (!v.isDouble() || !std::isfinite(v.toDouble()) || v.toDouble() < lo || v.toDouble() > hi)
            throw std::runtime_error("Invalid camera value");
        return float(v.toDouble());
    };
    if (a.contains("position")) {
        auto p = a["position"].toArray();
        if (p.size() != 3)
            throw std::runtime_error("Camera position requires [x,y,z]");
        camera = {number(p[0], -1000000, 1000000), number(p[1], -1000000, 1000000),
                  number(p[2], -1000000, 1000000)};
    }
    if (a.contains("yaw"))
        yaw = number(a["yaw"], -360000, 360000);
    if (a.contains("pitch"))
        pitch = number(a["pitch"], -89, 89);
    if (a["reset"].toBool()) {
        resetCamera();
        return;
    }
    stopInput();
    m_camera = camera;
    m_yaw = yaw;
    m_pitch = pitch;
    m_pivot = m_camera + forward() * 75.f;
    changedCamera();
}

void Viewport::jumpTo(int x, int z) {
    if (!m_editor)
        return;
    stopInput();
    m_speed = 35;
    const auto &w = m_editor->world();
    x = std::clamp(x, w.originX, w.originX + w.width - 1);
    z = std::clamp(z, w.originZ, w.originZ + w.depth - 1);
    m_pivot = {x + .5f, float(w.height(x, z) + 1), z + .5f};
    m_camera = m_pivot - QVector3D(-.5f, -.6f, -.65f).normalized() * 75;
    const auto f = (m_pivot - m_camera).normalized();
    m_yaw = qRadiansToDegrees(std::atan2(f.x(), -f.z()));
    m_pitch = qRadiansToDegrees(std::asin(f.y()));
    changedCamera();
}

bool Viewport::dropAsset(const QString &id, double x, double y) {
    if (!m_editor)
        return false;
    pick({x, y});
    bool ok = m_hit.valid && m_editor->placeAssetInstance(id, m_hit.x, m_hit.y + 1, m_hit.z);
    if (ok)
        m_ghostUntilRevision = m_editor->revision();
    else
        clearAssetPreview();
    return ok;
}
void Viewport::clearAssetPreview() {
    m_ghostVisible = false;
    m_ghostVolume = false;
    m_ghostUntilRevision = 0;
    update();
    emit cameraChanged();
}
void Viewport::previewAsset(const QString &id, double x, double y) {
    if (!m_editor || m_editor->commandBusy())
        return;
    if (x < 0 || y < 0 || x >= width() || y >= height()) {
        clearAssetPreview();
        return;
    }
    pick({x, y});
    if (!m_hit.valid) {
        clearAssetPreview();
        return;
    }
    auto &state = m_editor->world().creation;
    const QVector3D at(m_hit.x, m_hit.y + 1, m_hit.z);
    auto showBox = [&](const QVector3D &low, const QVector3D &high, const QString &name,
                       QVector3D angles = {}) {
        m_ghostVisible = false;
        m_ghostVolume = true;
        m_ghostName = name;
        m_ghostMin = low + at;
        m_ghostMax = high + at;
        wb::PcgVolume volume;
        volume.min = m_ghostMin;
        volume.max = m_ghostMax;
        volume.rotation = angles;
        m_ghostBoundsPose = volume.orientation();
        update();
        emit cameraChanged();
    };
    for (const auto &a : state.pcgAssets)
        if (a.id == id)
            return showBox(a.volume.min, a.volume.max, "PCG 范围预览", a.volume.rotation);
    for (const auto &g : state.graphs)
        if (g.id == id)
            return showBox({-32, -32, -32}, {32, 32, 32}, "PCG 范围预览");
    for (const auto &t : m_editor->world().heights.instances)
        if (t.id == id)
            return showBox({-t.width * .5f, 0, -t.depth * .5f},
                           {t.width * .5f, float(t.high - t.low), t.depth * .5f}, "地貌覆盖范围");
    m_ghostVolume = false;
    auto a = std::find_if(state.assets.begin(), state.assets.end(), [&](auto &a) { return a.id == id; });
    if (a == state.assets.end())
        return;
    if ((m_ghostId != id || m_ghostSourceRevision != m_editor->revision()) && !m_ghostWatcher.isRunning()) {
        m_ghostId = id;
        m_ghostSourceRevision = m_editor->revision();
        m_ghostMesh.reset();
        auto world = m_editor->world();
        auto asset = *a;
        m_ghostWatcher.setFuture(QtConcurrent::run([world = std::move(world), asset]() mutable {
            try {
                return wb::buildAssetGhost(std::move(world), asset);
            } catch (...) {
                return wb::MeshPtr{};
            }
        }));
    }
    m_ghostVisible = m_ghostId == id;
    m_ghostBoundsPose.setToIdentity();
    m_ghostTransform.setToIdentity();
    m_ghostTransform.translate(QVector3D(m_hit.x + .5f, m_hit.y + 1.5f, m_hit.z + .5f));
    m_ghostTransform.rotate(-a->defaultRotation.y(), 0, 1, 0);
    m_ghostTransform.scale(a->defaultScale.x() < 0 ? -1 : 1, 1, a->defaultScale.z() < 0 ? -1 : 1);
    m_ghostTransform.translate(-.5, -.5, -.5);
    m_ghostTransform.scale(std::abs(a->defaultScale.x()), a->defaultScale.y(), std::abs(a->defaultScale.z()));
    m_ghostTransform.translate(-a->pivot);
    m_ghostVolume = true;
    m_ghostName = m_ghostMesh ? "资产放置预览" : "正在准备资产预览";
    m_ghostMin = {1e9f, 1e9f, 1e9f};
    m_ghostMax = {-1e9f, -1e9f, -1e9f};
    for (int i = 0; i < 8; ++i) {
        const auto p = m_ghostTransform.map(
            QVector3D(i & 1 ? a->width : 0, i & 2 ? a->height : 0, i & 4 ? a->depth : 0));
        for (int axis = 0; axis < 3; ++axis) {
            m_ghostMin[axis] = std::min(m_ghostMin[axis], p[axis]);
            m_ghostMax[axis] = std::max(m_ghostMax[axis], p[axis]);
        }
    }

    update();
    emit cameraChanged();
}
QVariantList Viewport::creationOverlay() const {
    QVariantList objects;
    if (!m_editor)
        return objects;
    const auto &creation = m_editor->renderCreation();
    auto add = [&](const QString &id, const QString &name, QVector3D center, const QString &kind) {
        bool visible;
        const auto c = projectPoint(center, &visible);
        if (visible)
            objects << QVariantMap{{"id", id},
                                   {"name", name},
                                   {"x", c.x()},
                                   {"y", c.y()},
                                   {"selected", id == m_editor->selectedNode()},
                                   {"kind", kind}};
    };
    if (m_ghostVolume && (!m_ghostVisible || !m_ghostMesh))
        add("placement-preview", m_ghostName, (m_ghostMin + m_ghostMax) * .5f, "volume");
    for (const auto &v : creation.volumes)
        if (v.visible)
            add(v.id, v.name, (v.min + v.max) * .5f, "volume");
    for (const auto &p : creation.presets)
        if (p.enabled)
            add(p.id, p.name, p.position, "asset");
    for (const auto &n : creation.roadNodes)
        if (n.enabled)
            add(n.id, n.name, n.position + QVector3D(0, 1, 0), "road");
    return objects;
}
