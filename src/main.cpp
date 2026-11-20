#include "core/ProfileController.hpp"
#include "core/ProfileStore.hpp"
#include "dbus/NetworkManagerWatcher.hpp"
#include "dbus/PkexecChromePolicyWriter.hpp"
#include "dbus/SystemdResolvedBackend.hpp"
#include "ui/SettingsWindow.hpp"
#include "ui/TrayController.hpp"

#include <QApplication>
#include <QDir>
#include <QLockFile>
#include <QMessageBox>
#include <QStandardPaths>
#include <QSystemTrayIcon>

#include <cstdio>
#include <cstring>

#ifndef CONTROLD_VERSION
#error CONTROLD_VERSION must be the CMake project version
#endif

namespace {

bool versionRequested(int argc, char* argv[]) {
    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], "--version") == 0) {
            return true;
        }
    }
    return false;
}

QString settingsFilePath() {
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(directory);
    return directory + QStringLiteral("/settings.ini");
}

QString lockFilePath() {
    QString directory = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (directory.isEmpty()) {
        directory = QDir::tempPath();
    }
    QDir().mkpath(directory);
    return directory + QStringLiteral("/controld-tray.lock");
}

} // namespace

int main(int argc, char* argv[]) {
    if (versionRequested(argc, argv)) {
        std::fputs("controld-tray " CONTROLD_VERSION "\n", stdout);
        return 0;
    }

    QApplication application(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("vzhitko"));
    QApplication::setApplicationName(QStringLiteral("controld-tray"));
    QApplication::setApplicationVersion(QStringLiteral(CONTROLD_VERSION));
    QApplication::setApplicationDisplayName(QStringLiteral("ControlD Tray"));
    QApplication::setDesktopFileName(QStringLiteral("controld-tray"));
    QApplication::setQuitOnLastWindowClosed(false);

    QLockFile lock(lockFilePath());
    lock.setStaleLockTime(0);
    if (!lock.tryLock(100)) {
        QMessageBox::warning(nullptr, QStringLiteral("ControlD Tray"),
                             QStringLiteral("ControlD Tray is already running."));
        return 1;
    }

    controld::ProfileStore store(settingsFilePath());
    controld::SystemdResolvedBackend resolved;
    controld::NetworkManagerWatcher watcher;
    controld::PkexecChromePolicyWriter chrome(controld::defaultChromeHelperPath());
    controld::ProfileController controller(store, resolved, watcher, chrome);
    controld::SettingsWindow settings(controller);
    controld::TrayController tray(controller, settings);

    QObject::connect(&watcher, &controld::NetworkManagerWatcher::primaryLinkChanged, &controller,
                     &controld::ProfileController::onPrimaryLinkChanged);
    QObject::connect(&watcher, &controld::NetworkManagerWatcher::primaryLinkActivated, &controller,
                     &controld::ProfileController::onPrimaryLinkActivated);

    watcher.start();
    const controld::OperationResult started = controller.startup();
    if (!started.notification.isEmpty()) {
        tray.notify(QStringLiteral("ControlD Tray"), started.notification);
    }

    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        settings.showWithoutTray();
        QMessageBox::information(
            &settings, QStringLiteral("ControlD Tray"),
            QStringLiteral("No system tray is available. Use this window to choose a profile or "
                           "restore default DNS."));
    }

    return QApplication::exec();
}
