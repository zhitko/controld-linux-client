#include "core/AutostartManager.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTest>

class TestAutostart : public QObject {
    Q_OBJECT

  private slots:
    void init();
    void writesMarkedDesktopFileAndRefusesForeignFiles();
};

void TestAutostart::init() {
    const QString path = controld::AutostartManager::filePath();
    QFile::remove(path);
}

void TestAutostart::writesMarkedDesktopFileAndRefusesForeignFiles() {
    QString error;
    QVERIFY(!controld::AutostartManager::isEnabled());
    QVERIFY(controld::AutostartManager::setEnabled(true, QStringLiteral("/usr/bin/controld-tray"),
                                                   &error));
    QVERIFY(error.isEmpty());
    QVERIFY(controld::AutostartManager::isEnabled());

    QFile file(controld::AutostartManager::filePath());
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QString text = QString::fromUtf8(file.readAll());
    file.close();
    QVERIFY(text.contains(QStringLiteral("Exec=/usr/bin/controld-tray")));
    QVERIFY(text.contains(QStringLiteral("Icon=controld-tray")));
    QVERIFY(text.contains(QStringLiteral("X-ControlD-Tray-Autostart=true")));
    QVERIFY(text.contains(QStringLiteral("X-GNOME-Autostart-enabled=true")));

    QVERIFY(controld::AutostartManager::setEnabled(
        true, QStringLiteral("/home/me/ControlD Tray.AppImage"), &error));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QString quoted = QString::fromUtf8(file.readAll());
    file.close();
    QVERIFY(quoted.contains(QStringLiteral("Exec=\"/home/me/ControlD Tray.AppImage\"")));

    QFile iconSource(QDir::temp().filePath(QStringLiteral("controld-tray-icon.png")));
    QVERIFY(iconSource.open(QIODevice::WriteOnly));
    iconSource.write("png");
    iconSource.close();
    QVERIFY(controld::AutostartManager::installIcon(iconSource.fileName(), &error));
    QFile installedIcon(controld::AutostartManager::iconInstallPath());
    QVERIFY(installedIcon.open(QIODevice::ReadOnly));
    QCOMPARE(installedIcon.readAll(), QByteArray("png"));

    QVERIFY(controld::AutostartManager::setEnabled(false, QStringLiteral("/usr/bin/controld-tray"),
                                                   &error));
    QVERIFY(!QFile::exists(controld::AutostartManager::filePath()));

    QDir().mkpath(QFileInfo(controld::AutostartManager::filePath()).dir().absolutePath());
    QFile foreign(controld::AutostartManager::filePath());
    QVERIFY(foreign.open(QIODevice::WriteOnly));
    foreign.write("[Desktop Entry]\nType=Application\nExec=/usr/bin/other\n");
    foreign.close();
    QVERIFY(!controld::AutostartManager::setEnabled(false, QStringLiteral("/usr/bin/controld-tray"),
                                                    &error));
    QVERIFY(error.contains(QStringLiteral("was not created")));
    QVERIFY(QFile::exists(controld::AutostartManager::filePath()));
    QVERIFY(!controld::AutostartManager::isEnabled());
}

#include "TestAutostart.moc"

int main(int argc, char* argv[]) {
    QStandardPaths::setTestModeEnabled(true);
    TestAutostart test;
    return QTest::qExec(&test, argc, argv);
}
