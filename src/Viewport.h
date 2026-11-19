#pragma once
#include "Editor.h"
#include <QAbstractNativeEventFilter>
#include <QElapsedTimer>
#include <QMatrix4x4>
#include <QQuickRhiItem>
#include <QSet>
#include <QTimer>

class Viewport : public QQuickRhiItem, public QAbstractNativeEventFilter {
    Q_OBJECT
    Q_PROPERTY(Editor *editor READ editor WRITE setEditor NOTIFY editorChanged)
    Q_PROPERTY(QString cameraText READ cameraText NOTIFY cameraChanged)
    Q_PROPERTY(QString hitText READ hitText NOTIFY cameraChanged)
    Q_PROPERTY(bool rawInput READ rawInput WRITE setRawInput NOTIFY cameraChanged)
    Q_PROPERTY(bool rawActive READ rawActive NOTIFY cameraChanged)
    Q_PROPERTY(double sensitivity READ sensitivity WRITE setSensitivity NOTIFY cameraChanged)
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
    bool nativeEventFilter(const QByteArray &type, void *message, qintptr *result) override;
    void applyCamera(const QJsonObject &arguments);
    Editor *editor() const { return m_editor; }
    void setEditor(Editor *editor);
    QString cameraText() const;
    QString hitText() const;
    Q_INVOKABLE void resetCamera();
    Q_INVOKABLE void focusTarget();
    QMatrix4x4 viewProjection() const;
    QVector3D camera() const { return m_camera; }
    QVector3D orbitPivot() const { return m_pivot; }
    wb::Hit hit() const { return m_hit; }
    std::shared_ptr<std::atomic_int> frameCounter() const { return m_frames; }
  signals:
    void editorChanged();
    void cameraChanged();

  protected:
    QQuickRhiItemRenderer *createRenderer() override;
    void mousePressEvent(QMouseEvent *) override;
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
    float m_yaw = -38.66f, m_pitch = -19.36f, m_speed = 35;
    QPointF m_pointer, m_anchorGlobal;
    wb::Hit m_hit;
    QTimer m_timer;
    QElapsedTimer m_elapsed, m_stampClock;
    QSet<int> m_keys;
    Qt::MouseButtons m_buttons;
    bool m_sculpting = false, m_altNav = false, m_reverse = false;
    bool m_rawInput = true, m_rawActive = false;
    double m_sensitivity = .12;
    int m_rawPackets = 0;
    std::shared_ptr<std::atomic_int> m_frames = std::make_shared<std::atomic_int>(0);
};
