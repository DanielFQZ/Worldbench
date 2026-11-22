#pragma once
#include "Editor.h"
#include <QAbstractNativeEventFilter>
#include <QElapsedTimer>
#include <QMatrix4x4>
#include <QQuickRhiItem>
#include <QSet>
#include <QTimer>

struct RenderMetrics {
    std::atomic_uint64_t renderedSourceRevision{0};
    std::atomic_int submitMicros{0}, uploadKiB{0}, residentKiB{0}, draws{0};
    std::atomic_int effectKiB{0}, shadowUpdates{0}, reflectionUpdates{0};
    std::atomic_bool visibleWater{false};
};

class Viewport : public QQuickRhiItem, public QAbstractNativeEventFilter {
    Q_OBJECT
    Q_PROPERTY(Editor *editor READ editor WRITE setEditor NOTIFY editorChanged)
    Q_PROPERTY(QString renderText READ renderText NOTIFY metricsChanged)
    Q_PROPERTY(QString cameraText READ cameraText NOTIFY cameraChanged)
    Q_PROPERTY(QString hitText READ hitText NOTIFY cameraChanged)
    Q_PROPERTY(bool rawInput READ rawInput WRITE setRawInput NOTIFY cameraChanged)
    Q_PROPERTY(bool rawActive READ rawActive NOTIFY cameraChanged)
    Q_PROPERTY(double sensitivity READ sensitivity WRITE setSensitivity NOTIFY cameraChanged)
    Q_PROPERTY(bool flightInertia READ flightInertia WRITE setFlightInertia NOTIFY cameraChanged)
    Q_PROPERTY(QVariantMap gizmoOverlay READ gizmoOverlay NOTIFY cameraChanged)
    Q_PROPERTY(int gizmoMode READ gizmoMode WRITE setGizmoMode NOTIFY cameraChanged)
    Q_PROPERTY(bool snapEnabled READ snapEnabled WRITE setSnapEnabled NOTIFY cameraChanged)
    Q_PROPERTY(int rawPackets READ rawPackets NOTIFY cameraChanged)
  public:
    explicit Viewport(QQuickItem *parent = nullptr);
    ~Viewport() override;
    bool rawInput() const { return m_rawInput; }
    bool rawActive() const { return m_rawActive; }
    int rawPackets() const { return m_rawPackets; }
    double sensitivity() const { return m_sensitivity; }
    void setRawInput(bool value);
    void setSensitivity(double value);
    bool flightInertia() const { return m_flightInertia; }
    void setFlightInertia(bool value);
    bool nativeEventFilter(const QByteArray &type, void *message, qintptr *result) override;
    void applyCamera(const QJsonObject &arguments);
    QVariantMap gizmoOverlay() const;
    int gizmoMode() const { return m_gizmoMode; }
    Q_INVOKABLE void setGizmoMode(int mode);
    bool gizmoDragging() const { return m_gizmoDragging; }
    bool snapEnabled() const { return m_snapEnabled; }
    void setSnapEnabled(bool enabled);
    Editor *editor() const { return m_editor; }
    void setEditor(Editor *editor);
    QString cameraText() const;
    QString hitText() const;
    Q_INVOKABLE void resetCamera();
    Q_INVOKABLE void frameWorld();
    QString renderText() const;
    std::shared_ptr<RenderMetrics> renderMetrics() const { return m_metrics; }
    Q_INVOKABLE void focusTarget();
    Q_INVOKABLE void jumpTo(int x, int z);
    QMatrix4x4 viewProjection() const;
    QVector3D camera() const { return m_camera; }
    QVector3D orbitPivot() const { return m_pivot; }
    wb::Hit hit() const { return m_hit; }
    std::shared_ptr<std::atomic_int> frameCounter() const { return m_frames; }
  signals:
    void metricsChanged();
    void editorChanged();
    void cameraChanged();

  protected:
    QQuickRhiItemRenderer *createRenderer() override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void hoverMoveEvent(QHoverEvent *) override;
    void hoverLeaveEvent(QHoverEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void keyReleaseEvent(QKeyEvent *) override;
    void focusOutEvent(QFocusEvent *) override;
    void mouseUngrabEvent() override;

  private:
    QPointF projectPoint(QVector3D p, bool *visible = nullptr) const;
    bool pressGizmo(QPointF p);
    void moveGizmo(QPointF p);
    void finishGizmo(bool cancel = false);
    int hitGizmo(QPointF p, const QVariantMap &overlay) const;
    bool rotationVector(QPointF p, QVector3D &vector) const;
    int m_gizmoMode = 0, m_gizmoAxis = -1, m_gizmoHover = -1;
    bool m_gizmoDragging = false;
    QPointF m_gizmoStart, m_gizmoScreenCenter, m_gizmoScreenAxis;
    QPointF m_gizmoPlaneU, m_gizmoPlaneV, m_gizmoLast;
    QVector3D m_gizmoCenter;
    QVector3D m_rotationStart, m_rotationLast;
    bool m_rotationRay = false, m_snapEnabled = false;
    QString m_gizmoReadout;
    double m_gizmoAngle = 0;
    double m_gizmoWorldLength = 1;
    wb::TerrainInstance m_gizmoInitial;
    QVector3D forward() const;
    QVector3D right() const;
    void pick(QPointF pos);
    void stopInput();
    void tick();
    void changedCamera();
    void navigate(float dx, float dy);
    void beginRawInput();
    void endRawInput();
    Editor *m_editor = nullptr;
    QVector3D m_camera{80, 65, 100}, m_pivot{0, 20, 0};
    QVector3D m_velocity;
    bool m_flightInertia = true;
    float m_yaw = -38.66f, m_pitch = -19.36f, m_speed = 35;
    QPointF m_pointer, m_anchorGlobal;
    wb::Hit m_hit;
    QTimer m_timer, m_metricsTimer;
    QElapsedTimer m_toolCycleClock;
    bool m_toolCycleActive = false;
    int m_toolCycleLast = -1;
    std::shared_ptr<RenderMetrics> m_metrics = std::make_shared<RenderMetrics>();
    QElapsedTimer m_elapsed, m_stampClock;
    QSet<int> m_keys;
    Qt::MouseButtons m_buttons;
    bool m_sculpting = false, m_altNav = false, m_reverse = false;
    bool m_selecting = false;
    QPoint m_selectionStart;
    bool m_rawInput = true, m_rawActive = false;
    double m_sensitivity = .12;
    int m_rawPackets = 0;
    std::shared_ptr<std::atomic_int> m_frames = std::make_shared<std::atomic_int>(0);
};
