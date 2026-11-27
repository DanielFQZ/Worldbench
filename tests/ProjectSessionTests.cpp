#include "core/Project.h"
#include "core/ProjectSession.h"
#include <QCoreApplication>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>
#include <stdexcept>
namespace {
int checks = 0;
void check(bool ok, const char *message) { ++checks; if (!ok) throw std::runtime_error(message); }
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QString error;
    if (app.arguments().contains("--hold")) {
        wb::ProjectSession lease(app.arguments().last());
        if (!lease.acquire(error)) return 3;
        std::cout << "locked\n" << std::flush;
        QThread::sleep(30);
        return 0;
    }
    try {
        QTemporaryDir temp;
        const auto path = temp.filePath("共享工程.wbench");
        auto world = wb::World::create(wb::WorldOptions{32, 1, -60, 10, 0, true});
        check(wb::saveProject(path, world, error), "create project fixture");
        auto first = std::make_unique<wb::ProjectSession>(path);
        check(first->acquire(error), "first editor owns lease");
        wb::ProjectSession alias(temp.path() + "/./共享工程.wbench");
        check(!alias.acquire(error), "path alias cannot acquire second lease");
#ifdef Q_OS_WIN
        wb::ProjectSession caseAlias(path.toUpper());
        check(!caseAlias.acquire(error), "case alias cannot bypass lease");
#endif
        check(first->unchanged(error), "unchanged source accepted");
        world.setBlock(0, 12, 0, wb::Glass);
        check(wb::saveProject(path, world, error), "simulate external non-cooperating writer");
        QByteArray external;
        check(wb::ProjectSession::stamp(path, external, error) && !first->unchanged(error),
              "content changes detected independently of timestamp");
        auto old = wb::World::create(wb::WorldOptions{32, 1, -60, 10, 0, true});
        check(!wb::saveProject(path, old, error, [&](QString &message) { return first->unchanged(message); }),
              "guard rejects stale save before atomic replacement");
        QByteArray after;
        check(wb::ProjectSession::stamp(path, after, error) && external == after, "rejected save preserves external data");
        first.reset();
        check(alias.acquire(error), "closing editor releases lease");
        const auto childPath = temp.filePath("child.wbench");
        QProcess child;
        child.start(app.applicationFilePath(), {"--hold", childPath});
        check(child.waitForStarted(5000) && child.waitForReadyRead(5000) && child.readAllStandardOutput().contains("locked"),
              "separate process holds lease");
        wb::ProjectSession other(childPath);
        check(!other.acquire(error), "second process refuses active lease");
        child.kill(); child.waitForFinished(5000);
        check(other.acquire(error), "dead process lease recovers without time-based stealing");
        check(other.unchanged(error), "new file lease records absence");
        check(wb::saveProject(childPath, world, error) && other.refresh(error) && other.unchanged(error),
              "new save refreshes source stamp");
        check(QFile::remove(childPath) && !other.unchanged(error), "external deletion rejected");
        std::cout << "PASS " << checks << " project lease checks\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL after " << checks << " lease checks: " << e.what() << '\n';
        return 1;
    }
}
