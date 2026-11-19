#pragma once
#include <QLocalServer>
#include <QObject>
#include <QSet>
class Editor;
class QLocalSocket;
class McpServer : public QObject {
    Q_OBJECT
  public:
    explicit McpServer(Editor *editor);
    ~McpServer() override;
    void setEnabled(bool enabled);
    bool enabled() const { return m_server.isListening(); }
    QString configuration() const;
    QString activity() const { return m_activity; }

  private:
    Editor *m_editor;
    QLocalServer m_server;
    QSet<QLocalSocket *> m_clients;
    QString m_name, m_token, m_activity = "MCP 已关闭";
};
