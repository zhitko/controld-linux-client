#include "core/AppImageLaunch.hpp"
#include "core/HelperInstall.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

#include <unistd.h>

namespace {

QString scriptPath() {
#ifdef CONTROLD_INSTALL_SCRIPT
    return QStringLiteral(CONTROLD_INSTALL_SCRIPT);
#else
    return {};
#endif
}

QByteArray sha256(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&file);
    return hash.result().toHex();
}

int runInstaller(const QStringList& arguments, const QString& root, QString* error,
                 bool confirmTestRoot = true) {
    QProcess process;
    process.setProgram(QStringLiteral("python3"));
    process.setArguments(QStringList {scriptPath()} + arguments);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    if (root.isEmpty()) {
        environment.remove(QStringLiteral("CONTROLD_TRAY_INSTALL_ROOT"));
        environment.remove(QStringLiteral("CONTROLD_TRAY_TEST_INSTALL"));
    } else {
        environment.insert(QStringLiteral("CONTROLD_TRAY_INSTALL_ROOT"), root);
        if (confirmTestRoot) {
            environment.insert(QStringLiteral("CONTROLD_TRAY_TEST_INSTALL"),
                               QStringLiteral("allow-test-root"));
        } else {
            environment.remove(QStringLiteral("CONTROLD_TRAY_TEST_INSTALL"));
        }
    }
    process.setProcessEnvironment(environment);
    process.start();
    if (!process.waitForStarted(5000) || !process.waitForFinished(20000)) {
        if (error != nullptr) {
            *error = QStringLiteral("python3 did not finish");
        }
        return 99;
    }
    if (error != nullptr) {
        *error = QString::fromUtf8(process.readAllStandardError());
    }
    return process.exitCode();
}

QString policyDocument() {
    return QStringLiteral("<?xml version=\"1.0\"?>\n"
                          "<policyconfig>\n"
                          "  <action id=\"com.vzhitko.controldtray.chrome-policy\">\n"
                          "    <annotate "
                          "key=\"org.freedesktop.policykit.exec.path\">/usr/libexec/"
                          "controld-tray-chrome-policy</annotate>\n"
                          "  </action>\n"
                          "</policyconfig>\n");
}

} // namespace

class TestHelperInstall : public QObject {
    Q_OBJECT

  private slots:
    void appImageLaunchRequiresBothAbsolutePaths();
    void autostartUsesTheAppImagePath();
    void lddPlansOnlyLibrariesInsideTheAppDir();
    void promptsNameTheSystemPathsAndDoNotWriteYet();
    void manifestRejectsDestinationsOutsideTheHelperPaths();
    void inspectsAFakeRootWithoutRequiringRootOwnership();
    void prepareStagesFilesAndTheInstallerCopiesOnlyThosePaths();
    void installerRefusesSymlinksAndChangedFiles();
};

void TestHelperInstall::appImageLaunchRequiresBothAbsolutePaths() {
    QProcessEnvironment environment;
    QVERIFY(!controld::appImageLaunch(environment).active);

    environment.insert(QStringLiteral("APPIMAGE"), QStringLiteral("/opt/ControlD.AppImage"));
    QVERIFY(!controld::appImageLaunch(environment).active);

    environment.insert(QStringLiteral("APPDIR"), QStringLiteral("/tmp/mount"));
    const controld::AppImageLaunch launch = controld::appImageLaunch(environment);
    QVERIFY(launch.active);
    QCOMPARE(launch.appImagePath, QStringLiteral("/opt/ControlD.AppImage"));

    environment.insert(QStringLiteral("APPDIR"), QStringLiteral("/tmp/../etc"));
    QVERIFY(!controld::appImageLaunch(environment).active);
}

void TestHelperInstall::autostartUsesTheAppImagePath() {
    QProcessEnvironment environment;
    environment.insert(QStringLiteral("APPIMAGE"),
                       QStringLiteral("/home/me/ControlD Tray.AppImage"));
    environment.insert(QStringLiteral("APPDIR"), QStringLiteral("/tmp/.mount_controld"));
    QCOMPARE(controld::autostartExecutable(environment, QStringLiteral("/usr/bin/controld-tray")),
             QStringLiteral("/home/me/ControlD Tray.AppImage"));

    QProcessEnvironment plain;
    QCOMPARE(controld::autostartExecutable(plain, QStringLiteral("/usr/bin/controld-tray")),
             QStringLiteral("/usr/bin/controld-tray"));
}

void TestHelperInstall::lddPlansOnlyLibrariesInsideTheAppDir() {
    const QString text =
        QStringLiteral("\tlinux-vdso.so.1 (0x1)\n"
                       "\tlibQt6Core.so.6 => /opt/app/usr/lib/libQt6Core.so.6 (0x2)\n"
                       "\tlibc.so.6 => /lib/x86_64-linux-gnu/libc.so.6 (0x3)\n"
                       "\t/lib64/ld-linux-x86-64.so.2 (0x4)\n"
                       "\tlibssl.so.1.1 => not found\n");
    const QList<controld::LddEntry> entries = controld::parseLddOutput(text);
    QCOMPARE(entries.size(), 5);
    QVERIFY(controld::isHostRuntimeLibrary(QStringLiteral("libc.so.6")));
    QVERIFY(controld::isHostRuntimeLibrary(QStringLiteral("ld-linux-x86-64.so.2")));
    QVERIFY(controld::isHostRuntimeLibrary(QStringLiteral("libz.so.1")));
    QVERIFY(controld::isHostRuntimeLibrary(QStringLiteral("libcom_err.so.2")));
    QVERIFY(!controld::isHostRuntimeLibrary(QStringLiteral("libQt6Core.so.6")));
    QVERIFY(!controld::isHostRuntimeLibrary(QStringLiteral("libssl.so.1.1")));

    const controld::LibraryPlan missing =
        controld::planBundledLibraries(entries, QStringLiteral("/opt/app"));
    QVERIFY(!missing.ok);
    QVERIFY(missing.error.contains(QStringLiteral("libssl.so.1.1")));

    const QString inside =
        QStringLiteral("\tlibQt6Core.so.6 => /opt/app/usr/lib/libQt6Core.so.6 (0x2)\n"
                       "\tlibc.so.6 => /lib/x86_64-linux-gnu/libc.so.6 (0x3)\n"
                       "\tlibevil.so.0 => /usr/lib/libevil.so.0 (0x5)\n");
    const controld::LibraryPlan outside = controld::planBundledLibraries(
        controld::parseLddOutput(inside), QStringLiteral("/opt/app"));
    QVERIFY(!outside.ok);
    QVERIFY(outside.error.contains(QStringLiteral("outside")));

    const QString good =
        QStringLiteral("\tlibQt6Core.so.6 => /opt/app/usr/lib/libQt6Core.so.6 (0x2)\n"
                       "\tlibstdc++.so.6 => /usr/lib/x86_64-linux-gnu/libstdc++.so.6 (0x6)\n");
    const controld::LibraryPlan plan =
        controld::planBundledLibraries(controld::parseLddOutput(good), QStringLiteral("/opt/app"));
    QVERIFY(plan.ok);
    QCOMPARE(plan.libraries.size(), 1);
    QCOMPARE(plan.libraries.at(0).soname, QStringLiteral("libQt6Core.so.6"));
}

void TestHelperInstall::promptsNameTheSystemPathsAndDoNotWriteYet() {
    const QStringList paths = {controld::helperDestination(), controld::policyDestination(),
                               controld::manifestDestination()};
    const QString install = controld::helperInstallPrompt(paths);
    QVERIFY(install.contains(QStringLiteral("Nothing is written until you confirm")));
    QVERIFY(install.contains(QStringLiteral("does not need this helper")));
    QVERIFY(install.contains(controld::helperDestination()));
    QVERIFY(install.contains(controld::policyDestination()));
    QVERIFY(install.contains(QStringLiteral("/usr/lib/controld-tray/manifest")));

    const QString remove = controld::helperUninstallPrompt(paths);
    QVERIFY(remove.contains(QStringLiteral("does not change DNS")));
    QVERIFY(remove.contains(controld::policyDestination()));

    const controld::SystemHelperStatus missing;
    QVERIFY(controld::appImageHelperNote(missing).contains(
        QStringLiteral("without installing system files")));
}

void TestHelperInstall::manifestRejectsDestinationsOutsideTheHelperPaths() {
    QVERIFY(controld::installDestinationAllowed(controld::helperDestination()));
    QVERIFY(controld::installDestinationAllowed(
        QStringLiteral("/usr/lib/controld-tray/libQt6Core.so.6")));
    QVERIFY(!controld::installDestinationAllowed(QStringLiteral("/etc/passwd")));
    QVERIFY(!controld::installDestinationAllowed(
        QStringLiteral("/usr/lib/controld-tray/../../etc/passwd")));
    QVERIFY(!controld::installDestinationAllowed(
        QStringLiteral("/usr/lib/controld-tray/nested/lib.so")));

    QList<controld::ManifestEntry> entries;
    QString error;
    const QString bad =
        QStringLiteral("0644 /etc/passwd passwd %1\n").arg(QString(64, QLatin1Char('a')));
    QVERIFY(!controld::parseManifest(bad, &entries, &error));
    QVERIFY(!error.isEmpty());
}

void TestHelperInstall::inspectsAFakeRootWithoutRequiringRootOwnership() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString helper = root.path() + controld::helperDestination();
    const QString policy = root.path() + controld::policyDestination();
    QVERIFY(QDir().mkpath(QFileInfo(helper).dir().absolutePath()));
    QVERIFY(QDir().mkpath(QFileInfo(policy).dir().absolutePath()));

    controld::SystemHelperStatus missing = controld::inspectSystemHelper(root.path(), false);
    QVERIFY(!missing.ready);
    QVERIFY(missing.detail.contains(QStringLiteral("not installed")));

    QFile helperFile(helper);
    QVERIFY(helperFile.open(QIODevice::WriteOnly));
    helperFile.write("helper");
    helperFile.close();
    QVERIFY(helperFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                      QFileDevice::ExeOwner | QFileDevice::ReadGroup |
                                      QFileDevice::ExeGroup | QFileDevice::ReadOther |
                                      QFileDevice::ExeOther));
    QFile policyFile(policy);
    QVERIFY(policyFile.open(QIODevice::WriteOnly));
    policyFile.write(policyDocument().toUtf8());
    policyFile.close();

    const controld::SystemHelperStatus ready = controld::inspectSystemHelper(root.path(), false);
    QVERIFY(ready.ready);
    QVERIFY(!ready.managedByApp);

    QVERIFY(helperFile.setPermissions(helperFile.permissions() | QFileDevice::WriteOther));
    const controld::SystemHelperStatus writable = controld::inspectSystemHelper(root.path(), false);
    QVERIFY(!writable.ready);
    QVERIFY(writable.detail.contains(QStringLiteral("writable")));

    QVERIFY(helperFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                      QFileDevice::ExeOwner | QFileDevice::ReadGroup |
                                      QFileDevice::ExeGroup | QFileDevice::ReadOther |
                                      QFileDevice::ExeOther));
    const QString linkPath = helper + QStringLiteral(".link");
    QVERIFY(QFile::rename(helper, linkPath));
    QVERIFY(QFile::link(linkPath, helper));
    const controld::SystemHelperStatus linked = controld::inspectSystemHelper(root.path(), false);
    QVERIFY(!linked.ready);
    QVERIFY(linked.detail.contains(QStringLiteral("symlink")));
}

void TestHelperInstall::prepareStagesFilesAndTheInstallerCopiesOnlyThosePaths() {
    QVERIFY(QFile::exists(scriptPath()));
    QTemporaryDir appDir;
    QVERIFY(appDir.isValid());
    const QString helper =
        appDir.path() + QStringLiteral("/usr/libexec/controld-tray-chrome-policy");
    const QString policy =
        appDir.path() +
        QStringLiteral("/usr/share/polkit-1/actions/com.vzhitko.controldtray.policy");
    const QString script =
        appDir.path() + QStringLiteral("/usr/share/controld-tray/install_system_files.py");
    QVERIFY(QDir().mkpath(QFileInfo(helper).dir().absolutePath()));
    QVERIFY(QDir().mkpath(QFileInfo(policy).dir().absolutePath()));
    QVERIFY(QDir().mkpath(QFileInfo(script).dir().absolutePath()));
    QVERIFY(QFile::copy(QStringLiteral("/usr/bin/true"), helper));
    QVERIFY(QFile::copy(scriptPath(), script));
    QFile policyFile(policy);
    QVERIFY(policyFile.open(QIODevice::WriteOnly));
    policyFile.write(policyDocument().toUtf8());
    policyFile.close();

    controld::AppImageLaunch launch;
    launch.active = true;
    launch.appDir = appDir.path();
    launch.appImagePath = QStringLiteral("/opt/ControlD.AppImage");
    controld::HelperInstallSession session;
    QString error;
    QVERIFY2(session.prepare(launch, &error), qPrintable(error));
    QVERIFY(session.prompt().contains(QStringLiteral("Nothing is written until you confirm")));
    QVERIFY(session.prompt().contains(controld::helperDestination()));
    QVERIFY(QFile::exists(session.stagePath() + QStringLiteral("/manifest")));

    QTemporaryDir installed;
    QVERIFY(installed.isValid());
    QVERIFY2(runInstaller({QStringLiteral("install"), session.stagePath()}, installed.path(),
                          &error) == 0,
             qPrintable(error));
    const QString installedHelper = installed.path() + controld::helperDestination();
    const QString installedPolicy = installed.path() + controld::policyDestination();
    const QString installedManifest = installed.path() + controld::manifestDestination();
    QVERIFY(QFile::exists(installedHelper));
    QVERIFY(QFile::exists(installedPolicy));
    QVERIFY(QFile::exists(installedManifest));
    QCOMPARE(sha256(installedHelper), sha256(helper));
    const QFileDevice::Permissions permissions = QFileInfo(installedHelper).permissions();
    QVERIFY((permissions & QFileDevice::ExeOwner) != 0);
    QVERIFY((permissions & QFileDevice::WriteGroup) == 0);
    QVERIFY((permissions & QFileDevice::WriteOther) == 0);
    QVERIFY(!QFile::exists(installed.path() +
                           QStringLiteral("/etc/opt/chrome/policies/managed/controld.json")));

    const controld::SystemHelperStatus status =
        controld::inspectSystemHelper(installed.path(), false);
    QVERIFY(status.ready);
    QVERIFY(status.managedByApp);

    QCOMPARE(runInstaller({QStringLiteral("uninstall")}, installed.path(), &error), 0);
    QVERIFY(!QFile::exists(installedHelper));
    QVERIFY(!QFile::exists(installedPolicy));
    QVERIFY(!QFile::exists(installedManifest));
}

void TestHelperInstall::installerRefusesSymlinksAndChangedFiles() {
    QTemporaryDir stage;
    QTemporaryDir root;
    QVERIFY(stage.isValid());
    QVERIFY(root.isValid());
    const QString helperName = QStringLiteral("controld-tray-chrome-policy");
    const QString policyName = QStringLiteral("com.vzhitko.controldtray.policy");
    QFile helper(stage.path() + QLatin1Char('/') + helperName);
    QVERIFY(helper.open(QIODevice::WriteOnly));
    helper.write("#!/bin/true\n");
    helper.close();
    QFile policy(stage.path() + QLatin1Char('/') + policyName);
    QVERIFY(policy.open(QIODevice::WriteOnly));
    policy.write(policyDocument().toUtf8());
    policy.close();
    const QString helperHash = QString::fromLatin1(sha256(helper.fileName()));
    const QString policyHash = QString::fromLatin1(sha256(policy.fileName()));
    const QString manifestBody = QStringLiteral("0755 %1 %2 %3\n0644 %4 %5 %6\n")
                                     .arg(controld::helperDestination(), helperName, helperHash,
                                          controld::policyDestination(), policyName, policyHash);
    QFile manifest(stage.path() + QStringLiteral("/manifest"));
    QVERIFY(manifest.open(QIODevice::WriteOnly));
    manifest.write(manifestBody.toUtf8());
    manifest.close();

    QString error;
    QVERIFY2(runInstaller({QStringLiteral("install"), stage.path()}, root.path(), &error) == 0,
             qPrintable(error));

    QFile changed(root.path() + controld::helperDestination());
    QVERIFY(changed.open(QIODevice::WriteOnly | QIODevice::Truncate));
    changed.write("replaced by a package\n");
    changed.close();
    QCOMPARE(runInstaller({QStringLiteral("uninstall")}, root.path(), &error), 0);
    QVERIFY(QFile::exists(root.path() + controld::helperDestination()));
    QVERIFY(!QFile::exists(root.path() + controld::policyDestination()));
    QVERIFY(error.contains(QStringLiteral("Left")));

    const QString sentinelPath = root.path() + QStringLiteral("/sentinel");
    QFile sentinel(sentinelPath);
    QVERIFY(sentinel.open(QIODevice::WriteOnly));
    sentinel.write("secret");
    sentinel.close();
    const QString link = root.path() + controld::policyDestination();
    QVERIFY(QDir().mkpath(QFileInfo(link).dir().absolutePath()));
    QVERIFY(QFile::link(sentinelPath, link));
    QCOMPARE(runInstaller({QStringLiteral("install"), stage.path()}, root.path(), &error), 2);
    QFile reread(sentinelPath);
    QVERIFY(reread.open(QIODevice::ReadOnly));
    QCOMPARE(reread.readAll(), QByteArray("secret"));

    if (geteuid() == 0) {
        QString refused;
        QVERIFY(runInstaller({QStringLiteral("uninstall")}, root.path(), &refused, false) != 0);
        QVERIFY(refused.contains(QStringLiteral("Refusing")));
    } else {
        QString unprivileged;
        QVERIFY(runInstaller({QStringLiteral("uninstall")}, {}, &unprivileged) != 0);
        QVERIFY(unprivileged.contains(QStringLiteral("root")));
    }
}

#include "TestHelperInstall.moc"

QTEST_MAIN(TestHelperInstall)
