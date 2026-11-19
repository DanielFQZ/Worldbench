#include "Viewport.h"
#include <QCursor>
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
    std::unique_ptr<QRhiShaderResourceBindings> m_bindings;
    std::unique_ptr<QRhiGraphicsPipeline> m_opaque, m_alpha;
    std::vector<GpuChunk> m_chunks;
    std::shared_ptr<const wb::MeshScene> m_scene;
    QMatrix4x4 m_matrix;
    QVector3D m_camera;
    wb::Hit m_hit;
    int m_radius = 4, m_tool = 0;
    std::shared_ptr<std::atomic_int> m_frames;
    void initialize(QRhiCommandBuffer *) override {
        if (m_rhi != rhi()) {
            m_chunks.clear();
            m_opaque.reset();
            m_alpha.reset();
            m_bindings.reset();
            m_uniform.reset();
            m_rhi = rhi();
        }
        if (!m_uniform) {
            m_uniform.reset(rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 112));
            if (!m_uniform->create())
                qFatal("Unable to create uniform buffer");
            m_bindings.reset(rhi()->newShaderResourceBindings());
            m_bindings->setBindings({QRhiShaderResourceBinding::uniformBuffer(
                0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
                m_uniform.get())});
            if (!m_bindings->create())
                qFatal("Unable to create shader bindings");
        }
        auto pipeline = [&](bool alpha) {
            auto p = std::unique_ptr<QRhiGraphicsPipeline>(rhi()->newGraphicsPipeline());
            p->setShaderStages({{QRhiShaderStage::Vertex, shader(":/shaders/voxel.vert.qsb")},
                                {QRhiShaderStage::Fragment, shader(":/shaders/voxel.frag.qsb")}});
            QRhiVertexInputLayout layout;
            layout.setBindings({QRhiVertexInputBinding(sizeof(wb::Vertex))});
            layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float3, 0},
                                  {0, 1, QRhiVertexInputAttribute::Float3, 12},
                                  {0, 2, QRhiVertexInputAttribute::Float4, 24}});
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
    }
    void synchronize(QQuickRhiItem *item) override {
        auto *v = static_cast<Viewport *>(item);
        m_matrix = v->viewProjection();
        m_camera = v->camera();
        m_hit = v->hit();
        m_frames = v->frameCounter();
        if (v->editor()) {
            m_scene = v->editor()->scene();
            m_radius = v->editor()->radius();
            m_tool = v->editor()->tool();
        }
    }
    void render(QRhiCommandBuffer *cb) override {
        auto *updates = rhi()->nextResourceUpdateBatch();
        const QMatrix4x4 mvp = rhi()->clipSpaceCorrMatrix() * m_matrix;
        std::array<float, 28> u{};
        std::memcpy(u.data(), mvp.constData(), 64);
        u[16] = m_camera.x();
        u[17] = m_camera.y();
        u[18] = m_camera.z();
        u[20] = m_hit.x + .5f;
        u[21] = m_hit.y + .5f;
        u[22] = m_hit.z + .5f;
        u[23] = float(m_radius);
        u[24] = m_hit.valid ? 1.f : 0.f;
        u[25] = float(m_tool);
        updates->updateDynamicBuffer(m_uniform.get(), 0, 112, u.data());
        if (m_scene) {
            m_chunks.resize(m_scene->chunks.size());
            for (size_t i = 0; i < m_chunks.size(); ++i) {
                const auto &src = m_scene->chunks[i];
                auto &dst = m_chunks[i];
                if (!src || src->revision == dst.revision)
                    continue;
                auto upload = [&](const std::vector<wb::Vertex> &vertices,
                                  std::unique_ptr<QRhiBuffer> &buffer, int &count) {
                    count = int(vertices.size());
                    if (vertices.empty()) {
                        buffer.reset();
                        return;
                    }
                    const auto bytes = quint32(vertices.size() * sizeof(wb::Vertex));
                    if (!buffer || buffer->size() < bytes) {
                        buffer.reset(
                            rhi()->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, bytes));
                        if (!buffer->create())
                            qFatal("Unable to allocate voxel vertex buffer");
                    }
                    updates->uploadStaticBuffer(buffer.get(), 0, bytes, vertices.data());
                };
                upload(src->opaque, dst.opaque, dst.opaqueCount);
                upload(src->transparent, dst.transparent, dst.transparentCount);
                dst.revision = src->revision;
            }
        } else
            m_chunks.clear();
        cb->beginPass(renderTarget(), QColor("#202c3b"), {1.0f, 0}, updates);
        const auto sz = renderTarget()->pixelSize();
        cb->setViewport({0, 0, float(sz.width()), float(sz.height())});
        auto draw = [&](QRhiBuffer *buffer, int count) {
            if (!buffer || !count)
                return;
            QRhiCommandBuffer::VertexInput binding(buffer, 0);
            cb->setVertexInput(0, 1, &binding);
            cb->draw(count);
        };
        cb->setGraphicsPipeline(m_opaque.get());
        cb->setShaderResources(m_bindings.get());
        for (const auto &c : m_chunks)
            draw(c.opaque.get(), c.opaqueCount);
        cb->setGraphicsPipeline(m_alpha.get());
        cb->setShaderResources(m_bindings.get());
        std::vector<size_t> order;
        for (size_t i = 0; i < m_chunks.size(); ++i)
            if (m_chunks[i].transparentCount)
                order.push_back(i);
        if (m_scene)
            std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
                return (m_scene->chunks[a]->center - m_camera).lengthSquared() >
                       (m_scene->chunks[b]->center - m_camera).lengthSquared();
            });
        for (size_t i : order)
            draw(m_chunks[i].transparent.get(), m_chunks[i].transparentCount);
        cb->endPass();
        if (m_frames)
            ++(*m_frames);
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
        connect(e, &Editor::sceneChanged, this, [this] { update(); });
        connect(e, &Editor::changed, this, [this] { update(); });
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
    return m_hit.valid ? QString("%1 · (%2, %3, %4)")
                             .arg(QString::fromUtf8(wb::info(m_hit.block).name))
                             .arg(m_hit.x)
                             .arg(m_hit.y)
                             .arg(m_hit.z)
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
    p.perspective(60, float(std::max(1., width()) / std::max(1., height())), .15f, 3000);
    return p * v;
}
void Viewport::pick(QPointF pos) {
    m_pointer = pos;
    if (!m_editor || width() < 1 || height() < 1)
        return;
    const auto inv = viewProjection().inverted();
    QVector4D far =
        inv * QVector4D(float(pos.x() / width() * 2 - 1), float(1 - pos.y() / height() * 2), 1, 1);
    m_hit = m_editor->world().raycast(m_camera, far.toVector3DAffine() - m_camera);
    update();
    emit cameraChanged();
}
void Viewport::changedCamera() {
    m_pitch = std::clamp(m_pitch, -89.f, 89.f);
    pick(m_pointer);
    emit cameraChanged();
    update();
}
void Viewport::resetCamera() {
    stopInput();
    const auto &w = m_editor->world();
    m_pivot = {float(w.originX + w.size / 2), 20, float(w.originZ + w.size / 2)};
    m_camera = m_pivot + QVector3D(w.size * .625f, 45, w.size * .78125f);
    const auto f = (m_pivot - m_camera).normalized();
    m_yaw = qRadiansToDegrees(std::atan2(f.x(), -f.z()));
    m_pitch = qRadiansToDegrees(std::asin(f.y()));
    m_speed = 35;
    changedCamera();
}
void Viewport::focusTarget() {
    m_pivot = m_hit.valid ? m_hit.position : QVector3D(0, 20, 0);
    m_camera = m_pivot - forward() * (m_hit.valid ? 24.f : 150.f);
    changedCamera();
}
void Viewport::mousePressEvent(QMouseEvent *e) {
    forceActiveFocus();
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
        if (m_hit.valid)
            m_pivot = m_hit.position;
        m_anchorGlobal = e->globalPosition();
        setCursor(QCursor(Qt::BlankCursor));
    } else if (e->button() == Qt::LeftButton && m_editor) {
        m_sculpting = true;
        m_editor->beginStroke();
        m_editor->stamp(m_hit, m_reverse);
        m_stampClock.restart();
    }
    e->accept();
}
void Viewport::mouseReleaseEvent(QMouseEvent *e) {
    m_buttons = e->buttons();
    if (e->button() == Qt::LeftButton && m_sculpting) {
        if (m_editor)
            m_editor->endStroke();
        m_sculpting = false;
    }
    if (m_buttons == Qt::NoButton) {
        unsetCursor();
        m_keys.clear();
        m_altNav = false;
    }
    e->accept();
}
void Viewport::mouseMoveEvent(QMouseEvent *e) {
    m_reverse = e->modifiers().testFlag(Qt::ShiftModifier);
    const bool nav = m_altNav || m_buttons.testFlag(Qt::RightButton) || m_buttons.testFlag(Qt::MiddleButton);
    if (nav) {
        const QPointF delta = e->globalPosition() - m_anchorGlobal;
        if (delta.manhattanLength() < .1)
            return;
        const float dx = float(delta.x()), dy = float(delta.y());
        const float distance = std::max(5.f, (m_camera - m_pivot).length());
        if (m_buttons.testFlag(Qt::MiddleButton) ||
            (m_buttons.testFlag(Qt::LeftButton) && m_buttons.testFlag(Qt::RightButton))) {
            QVector3D move = (-right() * dx + QVector3D(0, 1, 0) * dy) * (distance * .0015f);
            m_camera += move;
            m_pivot += move;
        } else if (m_altNav && m_buttons.testFlag(Qt::RightButton)) {
            m_camera += forward() * (dx - dy) * distance * .005f;
        } else {
            m_yaw += dx * .2f;
            m_pitch = std::clamp(m_pitch - dy * .2f, -89.f, 89.f);
            if (m_altNav)
                m_camera = m_pivot - forward() * distance;
        }
        changedCamera();
        QCursor::setPos(m_anchorGlobal.toPoint());
    } else {
        pick(e->position());
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
        m_speed = std::clamp(m_speed * std::pow(1.25f, steps), 1.f, 500.f);
    else
        m_camera += forward() * steps * std::max(1.f, m_speed * .15f);
    changedCamera();
    e->accept();
}
void Viewport::hoverMoveEvent(QHoverEvent *e) {
    if (m_buttons == Qt::NoButton)
        pick(e->position());
}
void Viewport::hoverLeaveEvent(QHoverEvent *) {
    if (m_buttons == Qt::NoButton) {
        m_hit = {};
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
    m_keys.clear();
    m_buttons = Qt::NoButton;
    m_altNav = false;
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
        if (!move.isNull()) {
            m_camera += move.normalized() * m_speed * dt * (m_keys.contains(Qt::Key_Shift) ? 3.f : 1.f);
            changedCamera();
        }
    }
    if (m_sculpting && m_editor && m_stampClock.elapsed() > 100) {
        pick(m_pointer);
        m_editor->stamp(m_hit, m_reverse);
        m_stampClock.restart();
    }
}
