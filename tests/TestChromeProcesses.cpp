#include "core/ChromeProcesses.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <csignal>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

class TestChromeProcesses : public QObject {
    Q_OBJECT

  private slots:
    void identifiesBrowserBasenames();
    void planAndScanIgnoreOtherProcesses();
    void refusesToSignalInitOrSelf();
    void signalsASpawnedProcess();
};

void TestChromeProcesses::identifiesBrowserBasenames() {
    QVERIFY(controld::ChromeProcesses::isBrowserExecutable(
        QStringLiteral("/opt/google/chrome/chrome")));
    QVERIFY(controld::ChromeProcesses::isBrowserExecutable(
        QStringLiteral("/usr/lib/chromium/chromium")));
    QVERIFY(controld::ChromeProcesses::isBrowserExecutable(
        QStringLiteral("/usr/bin/chromium-browser")));
    QVERIFY(
        !controld::ChromeProcesses::isBrowserExecutable(QStringLiteral("/usr/bin/chromedriver")));
    QVERIFY(
        !controld::ChromeProcesses::isBrowserExecutable(QStringLiteral("/usr/bin/chrome-sandbox")));
}

void TestChromeProcesses::planAndScanIgnoreOtherProcesses() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString proc = dir.filePath(QStringLiteral("proc"));
    QVERIFY(QDir().mkpath(proc + QStringLiteral("/42")));
    QVERIFY(QDir().mkpath(proc + QStringLiteral("/7")));
    QVERIFY(
        QFile::link(QStringLiteral("/opt/google/chrome/chrome"), proc + QStringLiteral("/42/exe")));
    QVERIFY(QFile::link(QStringLiteral("/bin/sleep"), proc + QStringLiteral("/7/exe")));

    const auto scanned = controld::ChromeProcesses::scan(proc);
    QCOMPARE(scanned.size(), 2);
    const controld::RestartPlan plan = controld::ChromeProcesses::plan(scanned);
    QCOMPARE(plan.pids, QList<qint64>({42}));
    QCOMPARE(plan.executables, QStringList({QStringLiteral("/opt/google/chrome/chrome")}));
    QVERIFY(plan.refusal.isEmpty());

    const auto none = controld::ChromeProcesses::plan({});
    QCOMPARE(none.refusal, QStringLiteral("Chrome is not running."));

    const auto signalable = controld::ChromeProcesses::signalablePids({42, 7, 1}, scanned, 42);
    QVERIFY(signalable.isEmpty());
    const auto other = controld::ChromeProcesses::signalablePids({42, 7}, scanned, 99);
    QCOMPARE(other, QList<qint64>({42}));
}

void TestChromeProcesses::refusesToSignalInitOrSelf() {
    QString error;
    QVERIFY(!controld::ChromeProcesses::signalPid(1, SIGTERM, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!controld::ChromeProcesses::signalPid(qint64(::getpid()), SIGTERM, &error));
}

void TestChromeProcesses::signalsASpawnedProcess() {
    pid_t pid = 0;
    char sleepName[] = "sleep";
    char sleepTime[] = "30";
    char* argv[] = {sleepName, sleepTime, nullptr};
    const int spawned = posix_spawn(&pid, "/bin/sleep", nullptr, nullptr, argv, environ);
    QCOMPARE(spawned, 0);
    QVERIFY(pid > 1);

    QString error;
    QVERIFY(controld::ChromeProcesses::signalPid(pid, SIGTERM, &error));
    int status = 0;
    const pid_t waited = waitpid(pid, &status, 0);
    QCOMPARE(waited, pid);
    QVERIFY(WIFSIGNALED(status));
    QCOMPARE(WTERMSIG(status), SIGTERM);
}

QTEST_MAIN(TestChromeProcesses)
#include "TestChromeProcesses.moc"
