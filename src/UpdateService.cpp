#include "UpdateService.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTemporaryDir>
#include <QScopeGuard>
#include <QTimer>
#include <algorithm>
#include <array>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#include <aclapi.h>
#include <shellapi.h>
#endif

namespace {
constexpr auto ReleasesEndpoint = "https://api.github.com/repos/DanielFQZ/Worldbench/releases?per_page=10";
constexpr auto ManifestEndpoint = "https://raw.githubusercontent.com/DanielFQZ/Worldbench/main/updates/windows-x64.json";
constexpr auto ReleasesPage = "https://github.com/DanielFQZ/Worldbench/releases";
constexpr auto UserAgent = "Worldbench-UpdateChecker";
constexpr qint64 MaxCheckBytes = 2 * 1024 * 1024;
constexpr qint64 MaxDownloadBytes = 1024ll * 1024 * 1024;

bool trustedReleaseUrl(const QUrl &url, const QString &path) {
    return url.isValid() && url.scheme() == "https" && url.host() == "github.com" &&
           url.port() == -1 && url.userInfo().isEmpty() && !url.hasQuery() && !url.hasFragment() &&
           url.path() == path;
}

} // namespace

UpdateService::UpdateService(QObject *parent)
    : QObject(parent), m_currentVersion(QCoreApplication::applicationVersion()) {
    m_releaseUrl = QUrl(QString::fromLatin1(ReleasesPage));
    m_checkEndpoints = {QUrl(QString::fromLatin1(ManifestEndpoint)), QUrl(QString::fromLatin1(ReleasesEndpoint))};
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
    m_releaseUrl = QUrl(QString::fromLatin1(ReleasesPage));
    m_archiveAsset = {};
    m_installerAsset = {};
    m_checkSource = 0;
    m_checkErrors.clear();
    setStatus("正在读取更新清单…");
    emit stateChanged();
    requestCheckSource();
}

QString UpdateService::replyFailure(QNetworkReply *reply, const QByteArray &payload) {
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto message = QJsonDocument::fromJson(payload).object()["message"].toString();
    if ((status == 403 || status == 429) &&
        (reply->rawHeader("X-RateLimit-Remaining") == "0" || message.contains("rate limit", Qt::CaseInsensitive) || status == 429)) {
        QString detail = QString("GitHub 请求额度已用完（HTTP %1）").arg(status);
        bool ok = false;
        const qint64 reset = reply->rawHeader("X-RateLimit-Reset").toLongLong(&ok);
        if (ok && reset > 0)
            detail += "，预计 " + QDateTime::fromSecsSinceEpoch(reset).toLocalTime().toString("HH:mm:ss") + " 后恢复";
        return detail;
    }
    if (status)
        return QString("HTTP %1：%2").arg(status).arg(reply->errorString());
    return reply->errorString();
}

void UpdateService::requestCheckSource() {
    if (m_checkSource >= m_checkEndpoints.size()) {
        m_checking = false;
        const QString message = "无法检查更新。" + m_checkErrors.join("；") +
                                "。可以重试，或打开发布页下载安装包。";
        setStatus(message);
        emit failed(message);
        emit stateChanged();
        emit checkFinished(false);
        return;
    }
    const bool manifest = m_checkSource == 0;
    QNetworkRequest request(m_checkEndpoints[m_checkSource]);
    request.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(UserAgent));
    request.setRawHeader("Accept", manifest ? "application/json" : "application/vnd.github+json");
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    request.setTransferTimeout(8000);
    m_reply = m_network.get(request);
    QNetworkReply *reply = m_reply;
    connect(reply, &QIODevice::readyRead, this, [reply] {
        if (reply->bytesAvailable() > MaxCheckBytes) {
            reply->setProperty("worldbenchOversized", true);
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, manifest] {
        m_reply = nullptr;
        const auto payload = reply->readAll();
        QString error;
        if (reply->property("worldbenchOversized").toBool() || payload.size() > MaxCheckBytes) error = "更新响应过大";
        else if (reply->error() != QNetworkReply::NoError) error = replyFailure(reply, payload);
        else if (!parseRelease(payload, manifest)) error = m_parseError;
        reply->deleteLater();
        if (!error.isEmpty()) {
            m_checkErrors << (manifest ? "更新清单：" : "GitHub API：") + error;
            ++m_checkSource;
            if (m_checkSource < m_checkEndpoints.size()) setStatus("更新清单暂不可用，正在检查备用源…");
            requestCheckSource();
        }
    });
}

bool UpdateService::parseRelease(const QByteArray &payload, bool manifest) {
    const auto document = QJsonDocument::fromJson(payload);
    m_parseError.clear();
    if (manifest ? (!document.isObject() || document.object()["schema"].toInt() != 1 ||
                    !document.object()["releases"].isArray()) : !document.isArray()) {
        m_parseError = "更新响应格式无效";
        return false;
    }
    const auto releases = manifest ? document.object()["releases"].toArray() : document.array();
    QJsonObject release;
    static const QRegularExpression versionPattern("^v?\\d+\\.\\d+\\.\\d+(?:-[0-9A-Za-z.-]+)?$");
    for (const auto &value : releases) {
        const auto candidate = value.toObject();
        if (!candidate["draft"].toBool() && versionPattern.match(candidate["tag_name"].toString()).hasMatch() &&
            (release.isEmpty() || compareVersions(candidate["tag_name"].toString(), release["tag_name"].toString()) > 0)) {
            release = candidate;
        }
    }
    if (release.isEmpty()) {
        if (manifest || !releases.isEmpty()) {
            m_parseError = "更新清单中没有有效公开版本";
            return false;
        }
        m_checking = false;
        setStatus("没有可用的公开版本");
        emit checkFinished(false);
        return true;
    }
    m_latestVersion = release["tag_name"].toString();
    m_releaseNotes = release["body"].toString().trimmed();
    m_releaseUrl = QUrl(QString::fromLatin1(ReleasesPage) + "/tag/" + m_latestVersion);
    m_archiveAsset = {};
    m_installerAsset = {};
    for (const auto &value : release["assets"].toArray()) {
        const auto object = value.toObject();
        const QString name = object["name"].toString();
        const QUrl url(object["browser_download_url"].toString());
        const QString digest = object["digest"].toString();
        const auto hash = digest.mid(7).toLatin1().toLower();
        const qint64 size = object["size"].toInteger();
        if (!trustedReleaseUrl(url, "/DanielFQZ/Worldbench/releases/download/" + m_latestVersion + "/" + name) ||
            !name.startsWith("Worldbench-" + normalizedVersion(m_latestVersion)) ||
            name.contains('/') || name.contains('\\') || !digest.startsWith("sha256:") ||
            hash.size() != 64 || QByteArray::fromHex(hash).toHex() != hash || size <= 0 || size > MaxDownloadBytes)
            continue;
        if (name.endsWith("-win-x64.zip", Qt::CaseInsensitive) && m_archiveAsset.url.isEmpty())
            m_archiveAsset = {name, url, true, hash, size};
        else if (name.endsWith("-setup.exe", Qt::CaseInsensitive) && m_installerAsset.url.isEmpty())
            m_installerAsset = {name, url, false, hash, size};
    }
    if (manifest && m_archiveAsset.url.isEmpty() && m_installerAsset.url.isEmpty()) {
        m_parseError = "更新清单缺少有效的 Windows x64 文件或 SHA256";
        m_latestVersion.clear();
        m_releaseNotes.clear();
        m_releaseUrl = QUrl(QString::fromLatin1(ReleasesPage));
        return false;
    }
    m_checking = false;
    m_updateAvailable = compareVersions(m_latestVersion, m_currentVersion) > 0;
    if (!m_updateAvailable) {
        setStatus("当前已是最新版本 · " + displayVersion(m_currentVersion));
    } else if (m_archiveAsset.url.isEmpty() && m_installerAsset.url.isEmpty()) {
        m_updateAvailable = false;
        setStatus("发现新版本，但缺少有效的 Windows x64 文件或 SHA256，请打开发布页");
    } else {
        setStatus("发现新版本 · " + displayVersion(m_latestVersion));
    }
    emit stateChanged();
    emit checkFinished(m_updateAvailable);
    return true;
}

bool UpdateService::canUpdateInPlace(const QString &directory, QString &reason) {
    reason.clear();
    QStringList paths{directory};
    QDirIterator entries(directory, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                         QDirIterator::Subdirectories);
    while (entries.hasNext()) paths << entries.next();
#ifdef Q_OS_WIN
    HANDLE primary = nullptr, token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &primary)) {
        reason = "无法检查当前安装目录权限"; return false;
    }
    const bool duplicated = DuplicateToken(primary, SecurityImpersonation, &token);
    CloseHandle(primary);
    if (!duplicated) { reason = "无法检查当前用户权限"; return false; }
    const auto tokenGuard = qScopeGuard([&] { CloseHandle(token); });
#endif
    for (const auto &path : paths) {
        const QFileInfo file(path);
        bool writable = file.exists() && !file.isSymLink();
#ifdef Q_OS_WIN
        const auto native = QDir::toNativeSeparators(path);
        const auto wide = reinterpret_cast<const wchar_t *>(native.utf16());
        const DWORD attributes = GetFileAttributesW(wide);
        writable = writable && attributes != INVALID_FILE_ATTRIBUTES &&
                   !(attributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
                   (file.isDir() || !(attributes & FILE_ATTRIBUTE_READONLY));
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        if (writable && GetNamedSecurityInfoW(const_cast<wchar_t *>(wide), SE_FILE_OBJECT,
                         OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                         nullptr, nullptr, nullptr, nullptr, &descriptor) == ERROR_SUCCESS) {
            const auto descriptorGuard = qScopeGuard([&] { LocalFree(descriptor); });
            GENERIC_MAPPING mapping{FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS};
            DWORD desired = file.isDir() ? FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY : FILE_WRITE_DATA;
            DWORD granted = 0, privilegeBytes = sizeof(PRIVILEGE_SET);
            PRIVILEGE_SET privileges{}; BOOL allowed = FALSE;
            writable = AccessCheck(descriptor, token, desired, &mapping, &privileges, &privilegeBytes,
                                   &granted, &allowed) && allowed;
        } else writable = false;
#else
        writable = writable && file.isWritable();
#endif
        if (!writable) {
            reason = "安装目录中存在受保护或不可写的文件：" + path;
            return false;
        }
    }
    return true;
}

void UpdateService::installUpdate() {
    if (!m_updateAvailable || m_downloading)
        return;
    if (m_ready) {
        emit installReady();
        return;
    }
    QString reason;
    if (!canUpdateInPlace(QCoreApplication::applicationDirPath(), reason)) {
        if (!m_installerAsset.url.isEmpty()) download(m_installerAsset);
        else { setStatus(reason + "。请打开发布页获取安装器，尚未修改安装文件。"); emit failed(m_status); }
    } else if (!m_archiveAsset.url.isEmpty())
        download(m_archiveAsset);
    else if (!m_installerAsset.url.isEmpty())
        download(m_installerAsset);
}

void UpdateService::download(const ReleaseAsset &asset) {
    m_downloadIsArchive = asset.archive;
    m_downloading = true;
    m_ready = false;
    m_progress = 0;
    setStatus("正在下载 " + asset.name + "…");
    emit stateChanged();

    const QString base = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QTemporaryDir directory(QDir(base).filePath("Worldbench-update-download-XXXXXX"));
    if (!directory.isValid()) {
        m_downloading = false;
        setStatus("无法创建更新下载目录，请打开发布页手动下载");
        emit failed(m_status);
        emit stateChanged();
        return;
    }
    directory.setAutoRemove(false); // The detached updater consumes it after this process exits.
    m_downloadPath = directory.filePath(asset.name);
    QNetworkRequest request(asset.url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(UserAgent));
    request.setRawHeader("Accept", "application/octet-stream");
    request.setTransferTimeout(60000);
    m_reply = m_network.get(request);
    connect(m_reply, &QIODevice::readyRead, this, [this, asset] {
        if (m_reply && m_reply->bytesAvailable() > asset.size) m_reply->abort();
    });
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        m_progress = total > 0 ? int(std::clamp<qint64>(received * 100 / total, 0, 100)) : 0;
        emit stateChanged();
    });
    connect(m_reply, &QNetworkReply::finished, this, [this, asset] {
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        m_downloading = false;
        if (reply->error() != QNetworkReply::NoError) {
            const QString message = "更新下载失败：" + replyFailure(reply, reply->readAll());
            setStatus(message);
            emit failed(message);
            reply->deleteLater();
            emit stateChanged();
            return;
        }
        const auto data = reply->readAll();
        if (data.size() != asset.size || QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex() != asset.sha256) {
            const QString message = "更新文件完整性校验失败，未安装。请重试下载或打开发布页。";
            setStatus(message);
            emit failed(message);
            reply->deleteLater();
            emit stateChanged();
            return;
        }
        QSaveFile file(m_downloadPath);
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
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
        setStatus(asset.archive ? "更新已下载，准备安装" : "安装器已下载；受保护目录需要 Windows 管理员授权，取消授权将保留当前程序");
        emit stateChanged();
        emit installReady();
    });
}

void UpdateService::applyUpdate() {
    if (!m_ready || m_downloading || m_downloadPath.isEmpty()) return;
    QString reason;
    if (m_downloadIsArchive && !canUpdateInPlace(QCoreApplication::applicationDirPath(), reason)) {
        m_ready = false;
        if (!m_installerAsset.url.isEmpty()) download(m_installerAsset);
        else { setStatus(reason + "。尚未修改安装文件，请使用安装器。"); emit failed(m_status); emit stateChanged(); }
        return;
    }
    if (m_downloadIsArchive) launchArchiveUpdater(m_downloadPath);
    else launchInstaller(m_downloadPath);
}

void UpdateService::launchArchiveUpdater(const QString &path) {
    const QString scriptPath = QFileInfo(path).dir().filePath("WorldbenchUpdate.ps1");
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
    emit exitingForUpdate();
    QCoreApplication::quit();
}

QString UpdateService::installerParameters(const QString &directory, const QString &log, qint64 pid) {
    return QString("/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- /NOCLOSEAPPLICATIONS "
                   "/NORESTARTAPPLICATIONS /WBAUTOUPDATE=1 /WBPID=%1 /DIR=\"%2\" /LOG=\"%3\"")
        .arg(pid).arg(QDir::toNativeSeparators(directory), QDir::toNativeSeparators(log));
}

void UpdateService::launchInstaller(const QString &path) {
#ifdef Q_OS_WIN
    const QString directory = QDir::toNativeSeparators(QCoreApplication::applicationDirPath());
    const auto downloadDir = QFileInfo(path).dir();
    const QString logPath = downloadDir.filePath("installation.log");
    const QString monitorPath = downloadDir.filePath("WorldbenchInstallMonitor.ps1");
    QFile resource(":/WorldbenchInstallMonitor.ps1"), monitor(monitorPath);
    if (!resource.open(QIODevice::ReadOnly) || !monitor.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setStatus("无法准备更新监视器，当前程序保持打开"); return;
    }
    const auto bytes = resource.readAll();
    if (monitor.write(bytes) != bytes.size() || !monitor.flush()) {
        setStatus("无法写入更新监视器，当前程序保持打开"); return;
    }
    monitor.close();
    const QString native = QDir::toNativeSeparators(path);
    const QString parameters = installerParameters(directory, logPath, QCoreApplication::applicationPid());
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info); info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.lpVerb = L"runas";
    info.lpFile = reinterpret_cast<const wchar_t *>(native.utf16());
    info.lpParameters = reinterpret_cast<const wchar_t *>(parameters.utf16());
    info.nShow = SW_HIDE;
    if (!ShellExecuteExW(&info)) {
        setStatus(GetLastError() == ERROR_CANCELLED ? "已取消管理员授权，当前程序保持打开，安装文件未修改" :
                                                    "无法启动更新，当前程序保持打开，请重试");
        return;
    }
    QFile::remove(logPath + ".ready");
    const auto installerPid = GetProcessId(info.hProcess);
    const QStringList arguments{"-NoProfile", "-WindowStyle", "Hidden", "-ExecutionPolicy", "Bypass", "-File", monitorPath,
        "-InstallerProcessId", QString::number(installerPid), "-InstallDir", directory,
        "-ExpectedVersion", m_latestVersion, "-LogPath", logPath};
    const bool monitoring = installerPid && QProcess::startDetached("powershell.exe", arguments);
    if (info.hProcess) CloseHandle(info.hProcess);
    if (!monitoring) {
        // PrepareToInstall waits for us; keeping the editor alive makes it time out
        // without overwriting files when we cannot observe the result.
        setStatus("无法启动更新监视器；当前程序保持打开，安装将在等待超时后取消"); return;
    }
    m_ready = false;
    emit stateChanged();
    setStatus("正在准备后台更新，完成后将以普通权限重新打开 Worldbench");
    auto *handshake = new QTimer(this);
    handshake->setInterval(100);
    connect(handshake, &QTimer::timeout, this, [this, handshake, logPath, attempts = 0]() mutable {
        if (QFile::exists(logPath + ".ready")) {
            handshake->stop(); handshake->deleteLater();
            QFile::remove(logPath + ".ready");
            emit exitingForUpdate();
            QCoreApplication::quit();
        } else if (++attempts >= 100) {
            handshake->stop(); handshake->deleteLater();
            setStatus("更新监视器未就绪，当前程序保持打开；安装将等待超时后取消。日志：" + logPath);
        }
    });
    handshake->start();
#else
    setStatus("请使用适用于当前系统的安装器");
#endif
}

void UpdateService::openReleasePage() {
    if (m_releaseUrl.isValid())
        QDesktopServices::openUrl(m_releaseUrl);
}
