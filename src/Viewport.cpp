#include "Viewport.h"
#include "core/FarMesh.h"
#include <QCoreApplication>
#include <QCursor>
#include <QJsonArray>
#include <QSettings>
#include <stdexcept>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#endif
#include <QFile>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QWheelEvent>
#include <QtMath>
#include <algorithm>
#include <array>
#include <cstring>
#include <rhi/qrhi.h>

namespace {
QShader shader(const char *name) {
    QFile f(name);
    if (!f.open(QIODevice::ReadOnly))
        qFatal("Missing shader %s", name);
    return QShader::fromSerialized(f.readAll());
}
struct GpuChunk {
    std::unique_ptr<QRhiBuffer> opaque, transparent;
    int opaqueCount = 0, transparentCount = 0;
    uint64_t revision = 0;
};
class Renderer final : public QQuickRhiItemRenderer {
    QRhi *m_rhi = nullptr;
    std::unique_ptr<QRhiBuffer> m_uniform;
    std::unique_ptr<QRhiShaderResourceBindings> m_bindings, m_skyBindings;
    std::unique_ptr<QRhiTexture> m_texture;
    std::unique_ptr<QRhiSampler> m_sampler;
    std::shared_ptr<const wb::AssetLibrary> m_uploadedAssets;
    bool m_textureReady = false;
    std::unique_ptr<QRhiGraphicsPipeline> m_opaque, m_alpha, m_sky;
    std::map<wb::MeshPtr, GpuChunk, std::owner_less<wb::MeshPtr>> m_chunks;
    wb::Environment m_environment;
    std::shared_ptr<RenderMetrics> m_metrics;
    std::shared_ptr<const wb::MeshScene> m_scene;
    QMatrix4x4 m_matrix;
    QVector3D m_camera;
    wb::Hit m_hit;
    int m_radius = 4, m_tool = 0, m_brushShape = 0;
    double m_decay = 0;
    QRect m_selection;
    std::shared_ptr<std::atomic_int> m_frames;
    void initialize(QRhiCommandBuffer *) override {
        if (m_rhi != rhi()) {
            m_chunks.clear();
            m_opaque.reset();
            m_alpha.reset();
            m_sky.reset();
            m_bindings.reset();
            m_skyBindings.reset();
            m_uniform.reset();
            m_texture.reset();
            m_sampler.reset();
            m_uploadedAssets.reset();
            m_textureReady = false;
            m_rhi = rhi();
        }
        if (!m_uniform) {
            m_uniform.reset(rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 304));
            if (!m_uniform->create())
                qFatal("Unable to create uniform buffer");
            m_texture.reset(rhi()->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
            m_texture->create();
            m_sampler.reset(rhi()->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None,
                                              QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
            m_sampler->create();
            m_bindings.reset(rhi()->newShaderResourceBindings());
            m_bindings->setBindings(
                {QRhiShaderResourceBinding::uniformBuffer(
                     0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
                     m_uniform.get()),
                 QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                           m_texture.get(), m_sampler.get())});
            if (!m_bindings->create())
                qFatal("Unable to create shader bindings");
            // The sky has no atlas sampler. D3D11 caches native resource slots
            // on the SRB using the first pipeline that binds it. Sharing the
            // voxel SRB here can leave its texture unbound after an atlas swap.
            m_skyBindings.reset(rhi()->newShaderResourceBindings());
            m_skyBindings->setBindings({QRhiShaderResourceBinding::uniformBuffer(
                0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
                m_uniform.get())});
            if (!m_skyBindings->create())
                qFatal("Unable to create sky bindings");
        }
        auto pipeline = [&](bool alpha) {
            auto p = std::unique_ptr<QRhiGraphicsPipeline>(rhi()->newGraphicsPipeline());
            p->setShaderStages({{QRhiShaderStage::Vertex, shader(":/shaders/voxel.vert.qsb")},
                                {QRhiShaderStage::Fragment, shader(":/shaders/voxel.frag.qsb")}});
            QRhiVertexInputLayout layout;
            layout.setBindings({QRhiVertexInputBinding(sizeof(wb::Vertex))});
            layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float3, 0},
                                  {0, 1, QRhiVertexInputAttribute::Float3, 12},
                                  {0, 2, QRhiVertexInputAttribute::Float4, 24},
                                  {0, 3, QRhiVertexInputAttribute::Float2, 40}});
            p->setVertexInputLayout(layout);
            p->setTopology(QRhiGraphicsPipeline::Triangles);
            p->setCullMode(QRhiGraphicsPipeline::None);
            p->setDepthTest(true);
            p->setDepthWrite(!alpha);
            p->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
            if (alpha) {
                QRhiGraphicsPipeline::TargetBlend b;
                b.enable = true;
                b.srcColor = QRhiGraphicsPipeline::SrcAlpha;
                b.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
                p->setTargetBlends({b});
            }
            p->setSampleCount(renderTarget()->sampleCount());
            p->setShaderResourceBindings(m_bindings.get());
            p->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
            if (!p->create())
                qFatal("Unable to create voxel graphics pipeline");
            return p;
        };
        m_opaque = pipeline(false);
        m_alpha = pipeline(true);
        m_sky.reset(rhi()->newGraphicsPipeline());
        m_sky->setShaderStages({{QRhiShaderStage::Vertex, shader(":/shaders/sky.vert.qsb")},
                                {QRhiShaderStage::Fragment, shader(":/shaders/sky.frag.qsb")}});
        m_sky->setTopology(QRhiGraphicsPipeline::Triangles);
        m_sky->setCullMode(QRhiGraphicsPipeline::None);
        m_sky->setDepthTest(false);
        m_sky->setDepthWrite(false);
        m_sky->setSampleCount(renderTarget()->sampleCount());
        m_sky->setShaderResourceBindings(m_skyBindings.get());
        m_sky->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
        if (!m_sky->create())
            qFatal("Unable to create sky pipeline");
    }
    void synchronize(QQuickRhiItem *item) override {
        auto *v = static_cast<Viewport *>(item);
        m_matrix = v->viewProjection();
        m_camera = v->camera();
        m_hit = v->hit();
        m_frames = v->frameCounter();
        m_metrics = v->renderMetrics();
        if (v->editor()) {
            m_scene = v->editor()->scene();
            m_environment = v->editor()->world().environment;
            m_radius = v->editor()->radius();
            m_tool = v->editor()->tool();
            m_brushShape = v->editor()->brushShape();
            m_decay = v->editor()->brushDecay();
            m_selection = v->editor()->selectionRect();
        }
    }
    void render(QRhiCommandBuffer *cb) override {
        QElapsedTimer submit;
        submit.start();
        size_t uploadBytes = 0, residentBytes = 0;
        int draws = 0;
        auto *updates = rhi()->nextResourceUpdateBatch();
        auto assets = m_scene ? m_scene->assets : std::shared_ptr<const wb::AssetLibrary>{};
        if (!m_textureReady || assets != m_uploadedAssets) {
            QImage image = assets ? assets->atlas : wb::whiteAtlas();
            m_texture.reset(rhi()->newTexture(QRhiTexture::RGBA8, image.size()));
            if (!m_texture->create())
                qFatal("Unable to allocate texture atlas");
            m_bindings->setBindings(
                {QRhiShaderResourceBinding::uniformBuffer(
                     0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
                     m_uniform.get()),
                 QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                           m_texture.get(), m_sampler.get())});
            if (!m_bindings->create())
                qFatal("Unable to bind atlas");
            updates->uploadTexture(m_texture.get(), image);
            m_uploadedAssets = assets;
            m_textureReady = true;
        }
        const QMatrix4x4 mvp = rhi()->clipSpaceCorrMatrix() * m_matrix;
        std::array<float, 76> u{};
        std::memcpy(u.data(), mvp.constData(), 64);
        u[16] = m_camera.x();
        u[17] = m_camera.y();
        u[18] = m_camera.z();
        u[20] = m_hit.x + .5f;
        u[21] = m_hit.y + .5f;
        u[22] = m_hit.z + .5f;
        u[23] = float(m_radius);
        u[24] = m_hit.valid && m_tool >= 0 && m_tool <= 5 ? 1.f : 0.f;
        u[25] = m_tool == 5 ? 1.f : float(m_tool);
        u[26] = float(m_brushShape);
        u[27] = (m_tool == 0 || m_tool == 1 || m_tool == 5) ? float(1 - m_decay) : 1.f;
        u[28] = float(m_selection.left());
        u[29] = float(m_selection.top());
        u[30] = float(m_selection.right() + 1);
        u[31] = float(m_selection.bottom() + 1);
        u[32] = m_selection.isValid() ? 1.f : 0.f;
        auto sun = m_environment.sunDirection();
        u[36] = sun.x();
        u[37] = sun.y();
        u[38] = sun.z();
        u[39] = m_environment.sun ? m_environment.sunIntensity / 100.f : 0;
        auto color = [&](int offset, const QString &name, float w) {
            QColor c(name);
            u[offset] = c.redF();
            u[offset + 1] = c.greenF();
            u[offset + 2] = c.blueF();
            u[offset + 3] = w;
        };
        color(40, m_environment.skyColor, m_environment.sky ? m_environment.skyIntensity / 100.f : 0);
        color(44, m_environment.groundColor, std::exp2(m_environment.exposure / 10.f));
        color(48, m_environment.sunColor, m_environment.enabled ? 1.f : 0.f);
        color(52, m_environment.horizonColor, m_environment.atmosphere ? m_environment.haze / 100.f : 0);
        u[56] = m_environment.sky;
        u[57] = m_environment.sun;
        u[58] = m_environment.atmosphere;
        u[59] = rhi()->clipSpaceCorrMatrix()(1, 1);
        const auto inverse = m_matrix.inverted();
        std::memcpy(u.data() + 60, inverse.constData(), 64);
        updates->updateDynamicBuffer(m_uniform.get(), 0, 304, u.data());
        std::set<wb::MeshPtr, std::owner_less<wb::MeshPtr>> retained;
        std::vector<wb::MeshPtr> visible;
        if (m_scene) {
            auto add = [&](wb::MeshPtr src) {
                if (!src)
                    return;
                retained.insert(src);
                if (wb::visibleMesh(*src, m_matrix))
                    visible.push_back(src);
            };
            for (auto src : m_scene->chunks)
                add(src);
            for (const auto &t : m_scene->farTiles)
                add(t.mesh);
        }
        std::erase_if(m_chunks, [&](const auto &pair) { return !retained.contains(pair.first); });
        for (auto src : visible) {
            auto &dst = m_chunks[src];
            if (src->revision == dst.revision)
                continue;
            auto upload = [&](const std::vector<wb::Vertex> &vertices, std::unique_ptr<QRhiBuffer> &buffer,
                              int &count) {
                count = int(vertices.size());
                if (vertices.empty()) {
                    buffer.reset();
                    return;
                }
                const auto bytes = quint32(vertices.size() * sizeof(wb::Vertex));
                buffer.reset(rhi()->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, bytes));
                if (!buffer->create())
                    qFatal("Unable to allocate voxel vertex buffer");
                updates->uploadStaticBuffer(buffer.get(), 0, bytes, vertices.data());
                uploadBytes += bytes;
            };
            upload(src->opaque, dst.opaque, dst.opaqueCount);
            upload(src->transparent, dst.transparent, dst.transparentCount);
            dst.revision = src->revision;
        }
        for (const auto &[src, gpu] : m_chunks)
            residentBytes +=
                (gpu.opaque ? gpu.opaque->size() : 0) + (gpu.transparent ? gpu.transparent->size() : 0);
        cb->beginPass(renderTarget(), QColor("#202c3b"), {1.0f, 0}, updates);
        const auto sz = renderTarget()->pixelSize();
        cb->setViewport({0, 0, float(sz.width()), float(sz.height())});
        if (m_environment.enabled) {
            cb->setGraphicsPipeline(m_sky.get());
            cb->setShaderResources(m_skyBindings.get());
            cb->draw(3);
        }
        auto draw = [&](QRhiBuffer *buffer, int count) {
            if (!buffer || !count)
                return;
            QRhiCommandBuffer::VertexInput binding(buffer, 0);
            cb->setVertexInput(0, 1, &binding);
            cb->draw(count);
            ++draws;
        };
        cb->setGraphicsPipeline(m_opaque.get());
        cb->setShaderResources(m_bindings.get());
        for (auto src : visible) {
            const auto &c = m_chunks.at(src);
            draw(c.opaque.get(), c.opaqueCount);
        }
        cb->setGraphicsPipeline(m_alpha.get());
        cb->setShaderResources(m_bindings.get());
        std::sort(visible.begin(), visible.end(), [&](const auto &a, const auto &b) {
            return (a->center - m_camera).lengthSquared() > (b->center - m_camera).lengthSquared();
        });
        for (auto src : visible) {
            const auto &c = m_chunks.at(src);
            draw(c.transparent.get(), c.transparentCount);
        }
        cb->endPass();
        if (m_frames)
            ++(*m_frames);
        if (m_metrics) {
            m_metrics->submitMicros = int(submit.nsecsElapsed() / 1000);
            m_metrics->uploadKiB = int(uploadBytes / 1024);
            m_metrics->residentKiB = int(residentBytes / 1024);
            m_metrics->draws = draws;
        }
    }
};
} // namespace

Viewport::Viewport(QQuickItem *parent) : QQuickRhiItem(parent) {
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton | Qt::MiddleButton);
    setAcceptHoverEvents(true);
    setFlag(ItemIsFocusScope);
    setKeepMouseGrab(true);
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
    QCoreApplication::instance()->installNativeEventFilter(this);
}
QQuickRhiItemRenderer *Viewport::createRenderer() {
    return new Renderer;
}
void Viewport::setEditor(Editor *e) {
    if (m_editor == e)
        return;
    if (m_editor)
        disconnect(m_editor, nullptr, this, nullptr);
    m_editor = e;
    if (e) {
        e->setCameraController(this);
        connect(e, &Editor::sceneChanged, this, [this] { update(); });
        connect(e, &Editor::changed, this, [this] {
            update();
            emit cameraChanged();
        });
        connect(e, &Editor::worldReset, this, &Viewport::resetCamera);
    }
    emit editorChanged();
    update();
}
QString Viewport::cameraText() const {
    return QString("X %1   Y %2   Z %3   |   速度 %4")
        .arg(m_camera.x(), 0, 'f', 1)
        .arg(m_camera.y(), 0, 'f', 1)
        .arg(m_camera.z(), 0, 'f', 1)
        .arg(m_speed, 0, 'f', 0);
}
QString Viewport::hitText() const {
    return m_hit.valid ? QString("%1 · (%2, %3, %4)%5")
                             .arg(m_editor->world().palette[m_hit.block].label())
                             .arg(m_hit.x)
                             .arg(m_hit.y)
                             .arg(m_hit.z)
                             .arg(!m_editor->previewContains(m_hit.x, m_hit.z) &&
                                          (m_editor->tool() == 0 || m_editor->tool() == 1 || m_editor->tool() == 5)
                                      ? " · 远处精确落点 / 范围以实际格数为准" : "")
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
    if (m_editor) {
        const auto &w = m_editor->world();
        QVector3D center(float(w.originX + w.width / 2), 128, float(w.originZ + w.depth / 2));
        farPlane = std::max(3000.f, (m_camera - center).length() +
                                        std::hypot(float(w.width), float(w.depth)) + 1024.f);
    }
    p.perspective(60, float(std::max(1., width()) / std::max(1., height())), nearPlane, farPlane);
    return p * v;
}
void Viewport::pick(QPointF pos) {
    if (m_sculpting && pos == m_pointer && m_editor &&
        (m_editor->tool() == 0 || m_editor->tool() == 1 || m_editor->tool() == 5) && m_hit.valid) {
        const auto &w = m_editor->world();
        m_hit.y = w.height(m_hit.x, m_hit.z);
        m_hit.position = {m_hit.x + .5f, float(m_hit.y + 1), m_hit.z + .5f};
        m_hit.block = w.block(m_hit.x, m_hit.y, m_hit.z);
        m_hit.normal = {0, 1, 0};
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
    m_hit = world.raycast(m_camera, farPoint.toVector3DAffine() - m_camera, distance);
    if (m_hit.valid && m_editor->tool() >= 2 && m_editor->tool() <= 4 &&
        !m_editor->previewContains(m_hit.x, m_hit.z))
        m_hit = {};
    update();
    emit cameraChanged();
}
void Viewport::changedCamera() {
    m_pitch = std::clamp(m_pitch, -89.f, 89.f);
    if (m_editor) {
        auto p = m_camera + forward() * 60;
        if (m_camera.y() > 500 && forward().y() < -.05f)
            p = m_camera +
                forward() * ((m_editor->world().heights.baseHeight - m_camera.y()) / forward().y());
        m_editor->setPreviewCenter(int(p.x()), int(p.z()));
    }
    pick(m_pointer);
    emit cameraChanged();
    update();
}
void Viewport::resetCamera() {
    stopInput();
    const auto &w = m_editor->world();
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
    return QString("CPU提交 %1 ms · 上传 %2 MiB · 顶点缓冲 %3 MiB · %4 次绘制")
        .arg(m_metrics->submitMicros.load() / 1000., 0, 'f', 2)
        .arg(m_metrics->uploadKiB.load() / 1024., 0, 'f', 1)
        .arg(m_metrics->residentKiB.load() / 1024., 0, 'f', 1)
        .arg(m_metrics->draws.load());
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
    if (m_editor && m_editor->terrainDetails()["valid"].toBool()) {
        m_editor->focusTerrain();
        return;
    }
    m_pivot = m_hit.valid ? m_hit.position : QVector3D(0, 20, 0);
    m_camera = m_pivot - forward() * (m_hit.valid ? 24.f : 150.f);
    changedCamera();
}
void Viewport::mousePressEvent(QMouseEvent *e) {
    forceActiveFocus();
    if (e->buttons() == Qt::LeftButton && e->modifiers() == Qt::NoModifier && pressGizmo(e->position())) {
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
    } else if (e->button() == Qt::LeftButton && m_editor && m_editor->tool() == 6 && m_hit.valid) {
        m_selecting = true;
        m_selectionStart = {m_hit.x, m_hit.z};
        m_editor->setSelection(m_hit.x, m_hit.z, m_hit.x, m_hit.z);
    } else if (e->button() == Qt::LeftButton && m_editor && m_hit.valid && m_editor->tool() >= 0 && m_editor->tool() <= 5 &&
               !m_editor->assetBusy()) {
        m_sculpting = true;
        m_editor->beginStroke();
        m_editor->stamp(m_hit, m_reverse);
        m_stampClock.restart();
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
    if (e->key() == Qt::Key_Escape) {
        if (m_gizmoDragging)
            finishGizmo(true);
        stopInput();
        e->accept();
        return;
    }
    if (e->key() == Qt::Key_F) {
        focusTarget();
        e->accept();
        return;
    }
    if (e->key() == Qt::Key_BracketLeft) {
        m_editor->setRadius(m_editor->radius() - 1);
        e->accept();
        return;
    }
    if (e->key() == Qt::Key_BracketRight) {
        m_editor->setRadius(m_editor->radius() + 1);
        e->accept();
        return;
    }
    if (m_buttons.testFlag(Qt::RightButton) && !m_altNav) {
        m_keys.insert(e->key());
        e->accept();
        return;
    }
    if (!e->modifiers() && (e->key() == Qt::Key_W || e->key() == Qt::Key_E || e->key() == Qt::Key_R) &&
        m_editor->terrainDetails()["valid"].toBool()) {
        setGizmoMode(e->key() == Qt::Key_W ? 0 : e->key() == Qt::Key_E ? 1 : 2);
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
    stopInput();
    QQuickRhiItem::focusOutEvent(e);
}
void Viewport::mouseUngrabEvent() {
    stopInput();
}
void Viewport::tick() {
    const float dt = std::min(.05f, float(m_elapsed.restart()) * .001f);
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
