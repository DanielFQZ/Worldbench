#pragma once
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <memory>

namespace wb {
// Editing lease lives outside World snapshots/history. The sidecar coordinates
// Worldbench processes; the content stamp also detects non-cooperating writers.
class ProjectSession {
  public:
    static QString canonicalPath(const QString &path) {
        const QFileInfo info(path);
        if (info.exists()) return info.canonicalFilePath();
        const QFileInfo parent(info.absolutePath());
        return QDir(parent.canonicalFilePath().isEmpty() ? info.absolutePath() : parent.canonicalFilePath())
            .filePath(info.fileName());
    }
    static bool samePath(const QString &a, const QString &b) {
#ifdef Q_OS_WIN
        return canonicalPath(a).compare(canonicalPath(b), Qt::CaseInsensitive) == 0;
#else
        return canonicalPath(a) == canonicalPath(b);
#endif
    }
    static bool stamp(const QString &path, QByteArray &value, QString &error) {
        QFile file(path);
        if (!file.exists()) { value = "missing"; return true; }
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!file.open(QIODevice::ReadOnly) || file.size() > (1ll << 30) || !hash.addData(&file)) {
            error = "无法检查磁盘工程，未覆盖原文件：" + path;
            return false;
        }
        value = hash.result();
        return true;
    }
    explicit ProjectSession(const QString &path) : m_path(canonicalPath(path)), m_lock(m_path + ".lock") {
        // A document may remain open for days; elapsed time alone never breaks a lease.
        m_lock.setStaleLockTime(0);
    }
    bool acquire(QString &error) {
        if (!m_lock.tryLock()) {
            error = "工程正被其他窗口使用，或目录不允许创建锁文件；可只读查看并另存副本";
            return false;
        }
        return refresh(error);
    }
    const QString &path() const { return m_path; }
    void setBaseline(const QByteArray &value) { m_baseline = value; }
    bool refresh(QString &error) { m_baseline.clear(); return stamp(m_path, m_baseline, error); }
    bool unchanged(QString &error) const {
        QByteArray now;
        if (!stamp(m_path, now, error)) return false;
        if (m_baseline.isEmpty() || now != m_baseline) {
            error = "磁盘上的工程已被其他程序修改或删除，未覆盖文件。请另存为新文件，或重新打开磁盘版本";
            return false;
        }
        return true;
    }
  private:
    QString m_path;
    QLockFile m_lock;
    QByteArray m_baseline;
};
} // namespace wb
