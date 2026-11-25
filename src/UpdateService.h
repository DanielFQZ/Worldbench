#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QStringList>
#include <QUrl>

class QNetworkReply;

class UpdateService final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY stateChanged)
    Q_PROPERTY(QString releaseNotes READ releaseNotes NOTIFY stateChanged)
    Q_PROPERTY(QString releaseUrl READ releaseUrl NOTIFY stateChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(int progress READ progress NOTIFY stateChanged)
    Q_PROPERTY(bool checking READ checking NOTIFY stateChanged)
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY stateChanged)
    Q_PROPERTY(bool downloading READ downloading NOTIFY stateChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged)
public:
    explicit UpdateService(QObject *parent = nullptr);

    QString currentVersion() const { return m_currentVersion; }
    QString latestVersion() const { return m_latestVersion; }
    QString releaseNotes() const { return m_releaseNotes; }
    QString releaseUrl() const { return m_releaseUrl.toString(); }
    QString status() const { return m_status; }
    int progress() const { return m_progress; }
    bool checking() const { return m_checking; }
    bool updateAvailable() const { return m_updateAvailable; }
    bool downloading() const { return m_downloading; }
    bool ready() const { return m_ready; }

    Q_INVOKABLE void checkForUpdates(bool interactive = true);
    Q_INVOKABLE void installUpdate();
    Q_INVOKABLE void applyUpdate();
    Q_INVOKABLE void openReleasePage();

signals:
    void stateChanged();
    void checkFinished(bool updateAvailable);
    void failed(const QString &message);
    void installReady();
    void exitingForUpdate();

private:
    friend class UpdateServiceTests;
    struct ReleaseAsset {
        QString name;
        QUrl url;
        bool archive = false;
        QByteArray sha256;
        qint64 size = 0;
    };

    void setStatus(const QString &status);
    bool parseRelease(const QByteArray &payload, bool manifest = false);
    void requestCheckSource();
    static QString replyFailure(QNetworkReply *reply, const QByteArray &payload);
    void download(const ReleaseAsset &asset);
    void launchArchiveUpdater(const QString &path);
    void launchInstaller(const QString &path);
    static int compareVersions(const QString &left, const QString &right);
    static QString normalizedVersion(const QString &version);
    static QString displayVersion(const QString &version);

    QNetworkAccessManager m_network;
    QNetworkReply *m_reply = nullptr;
    QList<QUrl> m_checkEndpoints;
    int m_checkSource = 0;
    QStringList m_checkErrors;
    QString m_parseError;
    QString m_currentVersion;
    QString m_latestVersion;
    QString m_releaseNotes;
    QUrl m_releaseUrl;
    QString m_status;
    int m_progress = 0;
    bool m_checking = false;
    bool m_updateAvailable = false;
    bool m_downloading = false;
    bool m_ready = false;
    bool m_interactiveCheck = false;
    ReleaseAsset m_archiveAsset;
    ReleaseAsset m_installerAsset;
    QString m_downloadPath;
    bool m_downloadIsArchive = false;
};
