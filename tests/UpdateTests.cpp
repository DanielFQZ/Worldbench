#include "../src/UpdateService.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

namespace {
int checks = 0;
void check(bool value, const char *description) {
    ++checks;
    if (!value) throw std::runtime_error(description);
}
struct Response { int status = 200; QByteArray body; QByteArray headers; };
class HttpFixture {
public:
    QTcpServer server;
    QList<Response> responses;
    int requests = 0;
    HttpFixture() {
        check(server.listen(QHostAddress::LocalHost), "listen on local test server");
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this] {
            while (auto *socket = server.nextPendingConnection()) {
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                QObject::connect(socket, &QIODevice::readyRead, socket, [this, socket] {
                    if (socket->property("responded").toBool()) return;
                    auto request = socket->property("request").toByteArray() + socket->readAll();
                    socket->setProperty("request", request);
                    if (!request.contains("\r\n\r\n")) return;
                    socket->setProperty("responded", true);
                    const auto response = responses.value(requests++, {500, "unexpected request", {}});
                    socket->write("HTTP/1.1 " + QByteArray::number(response.status) + " Test\r\nContent-Length: " +
                                  QByteArray::number(response.body.size()) + "\r\nConnection: close\r\n" +
                                  response.headers + "\r\n" + response.body);
                    socket->disconnectFromHost();
                });
            }
        });
    }
    QUrl url(const QString &path) const {
        return QUrl(QString("http://127.0.0.1:%1/%2").arg(server.serverPort()).arg(path));
    }
};
QJsonObject release() {
    const QJsonObject asset{
        {"name", "Worldbench-0.26.3-pre.2-win-x64.zip"}, {"size", 123},
        {"digest", "sha256:" + QString(64, 'a')},
        {"browser_download_url", "https://github.com/DanielFQZ/Worldbench/releases/download/v0.26.3-pre.2/Worldbench-0.26.3-pre.2-win-x64.zip"}};
    return {{"tag_name", "v0.26.3-pre.2"}, {"draft", false},
            {"body", "修复更新检查"}, {"assets", QJsonArray{asset}}};
}
QByteArray manifest(QJsonObject r = release()) {
    return QJsonDocument(QJsonObject{{"schema", 1}, {"releases", QJsonArray{r}}}).toJson();
}
QByteArray api() { return QJsonDocument(QJsonArray{release()}).toJson(); }
} // namespace

class UpdateServiceTests {
public:
    static void run() {
        {
            QTemporaryDir directory;
            QFile file(directory.filePath("nested-file.bin"));
            check(file.open(QIODevice::WriteOnly) && file.write("fixture") == 7, "create permission fixture");
            file.close();
            QString reason;
            check(UpdateService::canUpdateInPlace(directory.path(), reason), "user-owned directory accepts in-place update");
            check(file.setPermissions(QFileDevice::ReadOwner | QFileDevice::ReadUser), "make read-only fixture");
            check(!UpdateService::canUpdateInPlace(directory.path(), reason) && reason.contains("nested-file.bin"), "nested read-only file routes to installer");
            file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadUser | QFileDevice::WriteUser);
        }
        {
            HttpFixture http;
            http.responses = {{200, manifest(), {}}};
            UpdateService service;
            service.m_checkEndpoints = {http.url("feed"), http.url("api")};
            QSignalSpy finished(&service, &UpdateService::checkFinished);
            service.checkForUpdates();
            service.checkForUpdates(); // No duplicate concurrent request.
            check(QTest::qWaitFor([&] { return !service.checking(); }, 5000), "static check completes");
            check(http.requests == 1 && finished.count() == 1, "valid static feed never consumes API quota");
            check(service.updateAvailable() && service.latestVersion() == "v0.26.3-pre.2", "feed includes prerelease upgrade");
            check(service.m_archiveAsset.sha256.size() == 64 && service.m_archiveAsset.size == 123, "download integrity metadata retained");
            service.m_currentVersion = "0.26.3-pre.2";
            check(service.parseRelease(manifest(), true) && !service.updateAvailable() && service.status().contains("最新版本"), "current version has explicit feedback");
            service.m_currentVersion = "0.26.4";
            check(service.parseRelease(manifest(), true) && !service.updateAvailable(), "older static feed never downgrades");
        }
        for (const auto response : {Response{404, "not found", {}}, Response{200, "invalid", {}},
                                    Response{200, "{\"schema\":2,\"releases\":[]}", {}}}) {
            HttpFixture http;
            http.responses = {response, {200, api(), {}}};
            UpdateService service;
            service.m_checkEndpoints = {http.url("feed"), http.url("api")};
            QSignalSpy finished(&service, &UpdateService::checkFinished);
            service.checkForUpdates();
            check(QTest::qWaitFor([&] { return !service.checking(); }, 5000), "fallback check completes");
            check(http.requests == 2 && service.updateAvailable() && finished.count() == 1, "bad static feed falls back once to API");
        }
        {
            HttpFixture http;
            http.responses = {{503, "unavailable", {}},
                              {403, "{\"message\":\"API rate limit exceeded\"}", "X-RateLimit-Remaining: 0\r\n"},
                              {200, manifest(), {}}};
            UpdateService service;
            service.m_checkEndpoints = {http.url("feed"), http.url("api")};
            service.checkForUpdates();
            check(QTest::qWaitFor([&] { return !service.checking(); }, 5000), "limited check completes");
            check(!service.updateAvailable() && service.status().contains("HTTP 403") && service.status().contains("额度"), "rate limit clearly diagnosed");
            check(service.releaseUrl() == "https://github.com/DanielFQZ/Worldbench/releases", "release page available even on total failure");
            service.checkForUpdates();
            check(QTest::qWaitFor([&] { return !service.checking(); }, 5000) && service.updateAvailable(), "retry recovers after failed check");
        }
        {
            HttpFixture http;
            http.responses = {{200, QByteArray(2 * 1024 * 1024 + 1, 'x'), {}}, {429, "limited", {}}};
            UpdateService service;
            service.m_checkEndpoints = {http.url("feed"), http.url("api")};
            service.checkForUpdates();
            check(QTest::qWaitFor([&] { return !service.checking(); }, 5000), "oversized reply completes");
            check(!service.updateAvailable() && service.status().contains("过大") && service.status().contains("429"), "oversized and limited responses rejected");
        }
        for (const QString mutation : {"host", "http", "hash", "size", "path", "draft"}) {
            auto r = release(); auto a = r["assets"].toArray()[0].toObject();
            if (mutation == "host") a["browser_download_url"] = "https://example.com/app.zip";
            if (mutation == "http") a["browser_download_url"] = a["browser_download_url"].toString().replace("https:", "http:");
            if (mutation == "hash") a["digest"] = "sha256:invalid";
            if (mutation == "size") a["size"] = -1;
            if (mutation == "path") a["name"] = "../../Worldbench-0.26.3-pre.2-win-x64.zip";
            if (mutation == "draft") r["draft"] = true;
            r["assets"] = QJsonArray{a};
            UpdateService service;
            check(!service.parseRelease(manifest(r), true) && !service.updateAvailable(), "invalid or unpublished manifest rejected");
        }
        for (const QByteArray data : {QByteArray("verified update content"), QByteArray(23, 'x'), QByteArray("short"), QByteArray(24, 'x')}) {
            HttpFixture http; http.responses = {{200, data, {}}};
            const QByteArray expected("verified update content");
            UpdateService service;
            service.m_updateAvailable = true;
            QSignalSpy ready(&service, &UpdateService::installReady);
            service.download({"Worldbench-fixture-win-x64.zip", http.url("download"), true,
                              QCryptographicHash::hash(expected, QCryptographicHash::Sha256).toHex(), expected.size()});
            check(QTest::qWaitFor([&] { return !service.downloading(); }, 5000), "download completes");
            if (data == expected) {
                QFile file(service.m_downloadPath);
                check(service.ready() && ready.count() == 1 && file.open(QIODevice::ReadOnly) && file.readAll() == expected,
                      "verified complete download alone becomes installable");
            } else check(!service.ready() && ready.isEmpty() && !QFile::exists(service.m_downloadPath), "bad or truncated download never installs");
            const QString directory = QFileInfo(service.m_downloadPath).absolutePath();
            QFile::remove(service.m_downloadPath);
            QDir().rmdir(directory); // Only this test-created, now-empty directory.
        }
    }
    static void permissionCheck(const QString &path) {
        QString reason;
        const bool allowed = UpdateService::canUpdateInPlace(path, reason);
        std::cout << "in_place=" << allowed << " " << reason.toStdString() << std::endl;
        check(!allowed, "protected installation must use installer");
    }
    static void live(bool download) {
        UpdateService service;
        service.m_currentVersion = "0.26.2-pre.3-updatetest";
        service.checkForUpdates();
        check(QTest::qWaitFor([&] { return !service.checking(); }, 25000), "live check completes");
        std::cout << service.status().toStdString() << std::endl;
        check(service.updateAvailable() && service.m_checkSource == 0, "public static feed works without GitHub API");
        if (download) {
            // No applyUpdate call: exercise public download and hash validation without replacing an installation.
            service.installUpdate();
            check(QTest::qWaitFor([&] { return !service.downloading(); }, 180000), "live download completes");
            std::cout << service.status().toStdString() << std::endl;
            check(service.ready(), "public update ZIP passes SHA256 and byte-length checks");
            std::cout << "verified_download=" << service.m_downloadPath.toStdString() << std::endl;
        }
    }
};
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    app.setApplicationName("Worldbench update tests");
    app.setApplicationVersion("0.26.2-pre.3-updatetest");
    try {
        if (app.arguments().contains("--permission-check")) UpdateServiceTests::permissionCheck(app.arguments().value(app.arguments().indexOf("--permission-check")+1));
        else if (app.arguments().contains("--live-check")) UpdateServiceTests::live(app.arguments().contains("--download"));
        else UpdateServiceTests::run();
        std::cout << "PASS " << checks << " update checks" << std::endl;
        return 0;
    } catch (const std::exception &e) { std::cerr << "FAIL " << e.what() << std::endl; return 1; }
}
