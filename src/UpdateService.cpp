#include "UpdateService.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QScopeGuard>
#include <algorithm>
#include <array>

namespace {
constexpr auto ReleasesEndpoint = "https://api.github.com/repos/DanielFQZ/Worldbench/releases?per_page=10";
constexpr auto UserAgent = "Worldbench-UpdateChecker";

} // namespace

UpdateService::UpdateService(QObject *parent)
    : QObject(parent), m_currentVersion(QCoreApplication::applicationVersion()) {
    setStatus("尚未检查更新");
}

void UpdateService::setStatus(const QString &status) {
    if (m_status == status)
        return;
    m_status = status;
    emit stateChanged();
}

QString UpdateService::normalizedVersion(const QString &version) {
    QString value = version.trimmed();
    if (value.startsWith('v', Qt::CaseInsensitive))
        value.remove(0, 1);
    const int plus = value.indexOf('+');
    if (plus >= 0)
        value.truncate(plus);
    return value;
}

QString UpdateService::displayVersion(const QString &version) {
    return normalizedVersion(version);
}

int UpdateService::compareVersions(const QString &left, const QString &right) {
    const QString a = normalizedVersion(left);
    const QString b = normalizedVersion(right);
    const auto split = [](const QString &value) {
        const int dash = value.indexOf('-');
        const QString core = dash < 0 ? value : value.left(dash);
        const QString suffix = dash < 0 ? QString() : value.mid(dash + 1);
        const auto parts = core.split('.');
        return std::pair<std::array<int, 3>, QString>{
            {parts.value(0).toInt(), parts.value(1).toInt(), parts.value(2).toInt()}, suffix};
    };
    const auto av = split(a);
    const auto bv = split(b);
    for (int i = 0; i < 3; ++i) {
        if (av.first[i] != bv.first[i])
            return av.first[i] < bv.first[i] ? -1 : 1;
    }
    const QString as = av.second, bs = bv.second;
    if (as.isEmpty() != bs.isEmpty())
        return as.isEmpty() ? 1 : -1;
    const auto ap = as.split('.'), bp = bs.split('.');
    for (int i = 0; i < std::min(ap.size(), bp.size()); ++i) {
        if (ap[i] == bp[i]) continue;
        bool an = false, bn = false;
        const auto ai = ap[i].toULongLong(&an), bi = bp[i].toULongLong(&bn);
        if (an && bn) return ai < bi ? -1 : 1;
        if (an != bn) return an ? -1 : 1;
        return ap[i].compare(bp[i], Qt::CaseInsensitive) < 0 ? -1 : 1;
    }
    return ap.size() == bp.size() ? 0 : ap.size() < bp.size() ? -1 : 1;
}

void UpdateService::checkForUpdates(bool interactive) {
    if (m_checking || m_downloading)
        return;
    m_interactiveCheck = interactive;
    m_checking = true;
    m_updateAvailable = false;
    m_ready = false;
    m_latestVersion.clear();
    m_releaseNotes.clear();
    m_releaseUrl.clear();
    m_archiveAsset = {};
    m_installerAsset = {};
    setStatus("正在检查 GitHub 更新…");
    emit stateChanged();

    QNetworkRequest request(QUrl(QString::fromLatin1(ReleasesEndpoint)));
    request.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(UserAgent));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setTransferTimeout(20000);
    m_reply = m_network.get(request);
    connect(m_reply, &QNetworkReply::finished, this, [this] {
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        const auto guard = qScopeGuard([this] {
            m_checking = false;
            emit stateChanged();
        });
        if (reply->error() != QNetworkReply::NoError) {
            const QString message = reply->error() == QNetworkReply::ContentNotFoundError
                                         ? "更新源暂时不可用，请稍后重试"
                                         : "无法连接 GitHub 更新源：" + reply->errorString();
            setStatus(message);
            emit failed(message);
            reply->deleteLater();
            emit checkFinished(false);
            return;
        }
        parseRelease(reply->readAll());
        reply->deleteLater();
    });
}

void UpdateService::parseRelease(const QByteArray &payload) {
    const auto document = QJsonDocument::fromJson(payload);
    if (!document.isArray()) {
        const QString message = "GitHub 更新响应格式无效";
        setStatus(message);
        emit failed(message);
        emit checkFinished(false);
        return;
    }
    QJsonObject release;
    for (const auto &value : document.array()) {
        const auto candidate = value.toObject();
        if (!candidate["draft"].toBool() && !candidate["tag_name"].toString().isEmpty() &&
            (release.isEmpty() || compareVersions(candidate["tag_name"].toString(), release["tag_name"].toString()) > 0)) {
            release = candidate;
        }
    }
    if (release.isEmpty()) {
        setStatus("没有可用的公开版本");
        emit checkFinished(false);
        return;
    }
    m_latestVersion = release["tag_name"].toString();
    m_releaseNotes = release["body"].toString().trimmed();
    m_releaseUrl = QUrl(release["html_url"].toString());
    for (const auto &value : release["assets"].toArray()) {
        const auto object = value.toObject();
        const QString name = object["name"].toString();
        const QUrl url(object["browser_download_url"].toString());
        if (!url.isValid())
            continue;
        if (name.endsWith("-win-x64.zip", Qt::CaseInsensitive) && m_archiveAsset.url.isEmpty())
            m_archiveAsset = {name, url, true};
        else if (name.endsWith("-setup.exe", Qt::CaseInsensitive) && m_installerAsset.url.isEmpty())
            m_installerAsset = {name, url, false};
    }
    m_updateAvailable = compareVersions(m_latestVersion, m_currentVersion) > 0;
    if (!m_updateAvailable) {
        setStatus("当前已是最新版本 · " + displayVersion(m_currentVersion));
    } else if (m_archiveAsset.url.isEmpty() && m_installerAsset.url.isEmpty()) {
        m_updateAvailable = false;
        setStatus("发现新版本，但发布页缺少 Windows x64 下载文件");
    } else {
        setStatus("发现新版本 · " + displayVersion(m_latestVersion));
    }
    emit stateChanged();
    emit checkFinished(m_updateAvailable);
}

void UpdateService::installUpdate() {
    if (!m_updateAvailable || m_downloading)
        return;
    if (!m_archiveAsset.url.isEmpty())
        download(m_archiveAsset);
    else if (!m_installerAsset.url.isEmpty())
        download(m_installerAsset);
}

void UpdateService::download(const ReleaseAsset &asset) {
    m_downloading = true;
    m_ready = false;
    m_progress = 0;
    setStatus("正在下载 " + asset.name + "…");
    emit stateChanged();

    const QString base = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QDir().mkpath(base);
    m_downloadPath = QDir(base).filePath(asset.name);
    QFile::remove(m_downloadPath);
    QNetworkRequest request(asset.url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(UserAgent));
    request.setRawHeader("Accept", "application/octet-stream");
    request.setTransferTimeout(60000);
    m_reply = m_network.get(request);
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        m_progress = total > 0 ? int(std::clamp<qint64>(received * 100 / total, 0, 100)) : 0;
        emit stateChanged();
    });
    connect(m_reply, &QNetworkReply::finished, this, [this, asset] {
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        m_downloading = false;
        if (reply->error() != QNetworkReply::NoError) {
            const QString message = "更新下载失败：" + reply->errorString();
            setStatus(message);
            emit failed(message);
            reply->deleteLater();
            emit stateChanged();
            return;
        }
        QSaveFile file(m_downloadPath);
        if (!file.open(QIODevice::WriteOnly) || file.write(reply->readAll()) < 0 || !file.commit()) {
            const QString message = "更新文件无法保存到临时目录";
            setStatus(message);
            emit failed(message);
            reply->deleteLater();
            emit stateChanged();
            return;
        }
        reply->deleteLater();
        m_progress = 100;
        m_ready = true;
        setStatus("更新已下载，准备安装");
        emit stateChanged();
        if (asset.archive)
            launchArchiveUpdater(m_downloadPath);
        else
            launchInstaller(m_downloadPath);
    });
}

void UpdateService::launchArchiveUpdater(const QString &path) {
    const QString scriptPath = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                                   .filePath("WorldbenchUpdate.ps1");
    QFile script(scriptPath);
    if (!script.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setStatus("无法准备更新程序，请下载安装器手动更新");
        return;
    }
    QFile resource(":/WorldbenchUpdate.ps1");
    if (!resource.open(QIODevice::ReadOnly) || script.write(resource.readAll()) < 0 || !script.flush()) {
        setStatus("无法准备更新程序，请下载安装器手动更新");
        return;
    }
    script.close();
    const QStringList arguments{
        "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", scriptPath, "-ArchivePath", path,
        "-InstallDir", QCoreApplication::applicationDirPath(), "-ProcessId", QString::number(QCoreApplication::applicationPid()),
        "-RestartPath", QCoreApplication::applicationFilePath()};
    if (!QProcess::startDetached("powershell.exe", arguments)) {
        setStatus("无法启动更新程序，请下载安装器手动更新");
        return;
    }
    setStatus("更新程序已启动，Worldbench 将关闭并完成更新");
    QCoreApplication::quit();
}

void UpdateService::launchInstaller(const QString &path) {
    if (!QProcess::startDetached(path, {})) {
        setStatus("无法启动安装器，请在临时目录手动运行下载文件");
        return;
    }
    setStatus("安装器已启动，Worldbench 将关闭");
    QCoreApplication::quit();
}

void UpdateService::openReleasePage() {
    if (m_releaseUrl.isValid())
        QDesktopServices::openUrl(m_releaseUrl);
}
