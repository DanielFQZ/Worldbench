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
#include <QQuickWindow>
#include <QWheelEvent>
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
}

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
    return QString("CPU提交 %1 ms · 上传 %2 MiB · 网格/颜色 %3 MiB · %4 次绘制 · 光影目标 %5 MiB")
        .arg(m_metrics->submitMicros.load() / 1000., 0, 'f', 2)
        .arg(m_metrics->uploadKiB.load() / 1024., 0, 'f', 1)
        .arg(m_metrics->residentKiB.load() / 1024., 0, 'f', 1)
        .arg(m_metrics->draws.load())
        .arg(m_metrics->effectKiB.load() / 1024., 0, 'f', 1);
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
    } else if (e->button() == Qt::LeftButton && m_editor && m_hit.valid && m_editor->tool() >= 0 &&
               m_editor->tool() <= 5 && !m_editor->assetBusy()) {
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
    if (matchesShortcut(e, m_editor->shortcut("deleteTerrain")) && !m_editor->commandBusy() &&
        !m_editor->exportBusy() && m_editor->terrainDetails()["valid"].toBool()) {
        m_editor->removeTerrain();
        e->accept();
        return;
    }
    if (e->key() == Qt::Key_Escape) {
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
    if (m_editor->terrainDetails()["valid"].toBool() &&
        (matchesShortcut(e, m_editor->shortcut("translateGizmo")) ||
         matchesShortcut(e, m_editor->shortcut("rotateGizmo")) ||
         matchesShortcut(e, m_editor->shortcut("scaleGizmo")))) {
        setGizmoMode(matchesShortcut(e, m_editor->shortcut("translateGizmo"))
                         ? 0
                         : matchesShortcut(e, m_editor->shortcut("rotateGizmo")) ? 1 : 2);
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
    // Animate shader normals without dirtying the world or rebuilding geometry.
    if (m_editor && isVisible() && window() && window()->isVisible() &&
        window()->visibility() != QWindow::Minimized && m_editor->scene()->waterVertices &&
        m_metrics->visibleWater && m_editor->world().environment.water && m_editor->world().environment.waterWaves &&
        m_editor->world().environment.waveStrength > 0)
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
