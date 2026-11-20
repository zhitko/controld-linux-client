#include "core/ChromeProcesses.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>

#include <cerrno>
#include <cstring>
#include <signal.h>
#include <unistd.h>

namespace controld {
namespace {

const QStringList kBrowserNames = {
    QStringLiteral("chrome"),
    QStringLiteral("chromium"),
    QStringLiteral("chromium-browser"),
    QStringLiteral("google-chrome"),
    QStringLiteral("google-chrome-stable"),
};

} // namespace

bool ChromeProcesses::isBrowserExecutable(const QString& executable) {
    return kBrowserNames.contains(QFileInfo(executable).fileName());
}

QList<ProcessImage> ChromeProcesses::scan(const QString& procRoot) {
    QList<ProcessImage> found;
    const QDir root(procRoot);
    if (!root.exists()) {
        return found;
    }
    const QStringList entries = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& entry : entries) {
        bool ok = false;
        const qint64 pid = entry.toLongLong(&ok);
        if (!ok || pid <= 0) {
            continue;
        }
        const QString target =
            QFile::symLinkTarget(procRoot + QLatin1Char('/') + entry + QStringLiteral("/exe"));
        if (target.isEmpty()) {
            continue;
        }
        ProcessImage image;
        image.pid = pid;
        image.executable = target;
        found.append(image);
    }
    return found;
}

RestartPlan ChromeProcesses::plan(const QList<ProcessImage>& processes) {
    RestartPlan plan;
    QStringList executables;
    for (const ProcessImage& process : processes) {
        if (process.pid <= 1 || !isBrowserExecutable(process.executable)) {
            continue;
        }
        plan.pids.append(process.pid);
        if (!executables.contains(process.executable)) {
            executables.append(process.executable);
        }
    }
    executables.sort();
    plan.executables = executables;
    if (plan.pids.isEmpty()) {
        plan.refusal = QStringLiteral("Chrome is not running.");
    }
    return plan;
}

QList<qint64> ChromeProcesses::signalablePids(const QList<qint64>& confirmed,
                                              const QList<ProcessImage>& live, qint64 selfPid) {
    QSet<qint64> allowed;
    for (const ProcessImage& process : live) {
        if (process.pid > 1 && process.pid != selfPid && isBrowserExecutable(process.executable)) {
            allowed.insert(process.pid);
        }
    }
    QList<qint64> pids;
    for (const qint64 pid : confirmed) {
        if (allowed.contains(pid)) {
            pids.append(pid);
        }
    }
    return pids;
}

bool ChromeProcesses::signalPid(qint64 pid, int signo, QString* error) {
    if (pid <= 1 || pid == qint64(::getpid())) {
        if (error != nullptr) {
            *error = QStringLiteral("Refusing to signal this process.");
        }
        return false;
    }
    if (::kill(static_cast<pid_t>(pid), signo) != 0) {
        if (error != nullptr) {
            *error = QString::fromLocal8Bit(strerror(errno));
        }
        return false;
    }
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

} // namespace controld
