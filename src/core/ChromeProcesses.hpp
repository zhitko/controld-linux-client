#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <QtGlobal>

namespace controld {

struct ProcessImage {
    qint64 pid = 0;
    QString executable;
};

struct RestartPlan {
    QStringList executables;
    QList<qint64> pids;
    QString refusal;
};

class ChromeProcesses {
  public:
    static bool isBrowserExecutable(const QString& executable);
    static QList<ProcessImage> scan(const QString& procRoot = QStringLiteral("/proc"));
    static RestartPlan plan(const QList<ProcessImage>& processes);
    static QList<qint64> signalablePids(const QList<qint64>& confirmed,
                                        const QList<ProcessImage>& live, qint64 selfPid);
    static bool signalPid(qint64 pid, int signo, QString* error);
};

} // namespace controld
