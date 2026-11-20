#include "ui/TrayController.hpp"

#include "core/AppImageLaunch.hpp"
#include "core/ChromeProcesses.hpp"
#include "core/HelperInstall.hpp"
#include "ui/HelperInstallDialog.hpp"
#include "ui/SettingsWindow.hpp"
#include "ui/StatusIcon.hpp"

#include <QProcessEnvironment>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCursor>
#include <QElapsedTimer>
#include <QMenu>
#include <QMessageBox>
#include <QThread>

#include <csignal>

namespace controld {

TrayController::TrayController(ProfileController& controller, SettingsWindow& settings,
                               QObject* parent)
    : QObject(parent), m_controller(controller), m_settings(settings),
      m_menu(new QMenu(&m_menuOwner)) {
    m_menuOwner.hide();
    m_tray.setIcon(StatusIcon::create(RuntimeStatus::Disabled));
    connect(&m_controller, &ProfileController::profilesChanged, this, &TrayController::rebuildMenu);
    connect(&m_controller, &ProfileController::statusChanged, this, &TrayController::rebuildMenu);
    connect(&m_controller, &ProfileController::statusChanged, this,
            &TrayController::updateAppearance);
    connect(&m_tray, &QSystemTrayIcon::activated, this, &TrayController::handleActivation);

    // Fill the menu before setContextMenu() and show(). Ubuntu's AppIndicator
    // exports it through com.canonical.dbusmenu and refuses to open a menu
    // that was empty at registration. Qt's AboutToShow reply always says the
    // layout is unchanged, so rebuilding from aboutToShow never reaches the
    // shell. Later profile changes rebuild this same menu object.
    rebuildMenu();
    m_tray.setContextMenu(m_menu);
    m_menuPreparedBeforeShow = m_tray.contextMenu() == m_menu && !m_menu->actions().isEmpty();
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        m_tray.show();
    }
    updateAppearance();
}

QMenu* TrayController::contextMenu() const {
    return m_tray.contextMenu();
}

bool TrayController::menuPreparedBeforeShow() const {
    return m_menuPreparedBeforeShow;
}

void TrayController::handleActivation(QSystemTrayIcon::ActivationReason reason) {
    // Left click arrives as Trigger on GNOME AppIndicator. Open the menu.
    // Context is the host's own right-click path; handling it here would
    // replace the menu the shell is already opening.
    if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
        const QRect geometry = m_tray.geometry();
        const QPoint position = geometry.isValid() ? geometry.center() : QCursor::pos();
        m_menu->popup(position);
    }
}

void TrayController::showSettings() {
    m_settings.show();
    m_settings.raise();
    m_settings.activateWindow();
}

void TrayController::notify(const QString& title, const QString& body) {
    if (body.isEmpty()) {
        return;
    }
    if (m_tray.isVisible()) {
        m_tray.showMessage(title, body, QSystemTrayIcon::Information, 10000);
        return;
    }
    QMessageBox::information(&m_settings, title, body);
}

void TrayController::rebuildMenu() {
    const auto groups = m_menu->findChildren<QActionGroup*>(QString(), Qt::FindDirectChildrenOnly);
    for (QActionGroup* group : groups) {
        delete group;
    }
    m_menu->clear();
    auto* status = m_menu->addAction(m_controller.statusLine());
    status->setEnabled(false);
    m_menu->addSeparator();

    auto* group = new QActionGroup(m_menu);
    group->setExclusive(true);
    const QString activeId = m_controller.store().activeProfileId();
    for (const Profile& profile : m_controller.store().profiles()) {
        auto* action =
            m_menu->addAction(QStringLiteral("%1 (%2)").arg(profile.name, profile.resolverId));
        action->setCheckable(true);
        action->setChecked(profile.id == activeId);
        group->addAction(action);
        const QString id = profile.id;
        connect(action, &QAction::triggered, this, [this, id]() { activate(id); });
    }

    auto* disableAction = m_menu->addAction(QStringLiteral("Disable (restore default DNS)"));
    disableAction->setCheckable(true);
    disableAction->setChecked(activeId.isEmpty() &&
                              m_controller.status() == RuntimeStatus::Disabled);
    group->addAction(disableAction);
    connect(disableAction, &QAction::triggered, this, &TrayController::disable);

    m_menu->addSeparator();
    connect(m_menu->addAction(QStringLiteral("Flush DNS cache")), &QAction::triggered, this,
            &TrayController::flush);
    connect(m_menu->addAction(QStringLiteral("Settings")), &QAction::triggered, this,
            &TrayController::showSettings);
    connect(m_menu->addAction(QStringLiteral("Restart Chrome")), &QAction::triggered, this,
            &TrayController::restartChrome);
    if (appImageLaunch(QProcessEnvironment::systemEnvironment()).active) {
        const SystemHelperStatus status = inspectSystemHelper({}, true);
        const bool updateAvailable = status.managedByApp && !status.groupedDnsAuthorization;
        if (!status.ready || updateAvailable) {
            connect(m_menu->addAction(updateAvailable ? QStringLiteral("Update helper")
                                                      : QStringLiteral("Install helper")),
                    &QAction::triggered, this, [this]() {
                        runHelperInstall(&m_settings);
                        rebuildMenu();
                    });
        }
        if (status.managedByApp) {
            connect(m_menu->addAction(QStringLiteral("Uninstall helper")), &QAction::triggered,
                    this, [this]() { runHelperUninstall(&m_settings, m_controller); });
        }
    }
    m_menu->addSeparator();
    connect(m_menu->addAction(QStringLiteral("Quit")), &QAction::triggered, qApp,
            &QApplication::quit);
}

void TrayController::updateAppearance() {
    m_tray.setIcon(StatusIcon::create(m_controller.status()));
    m_tray.setToolTip(m_controller.statusLine() + QStringLiteral("\n") +
                      m_controller.statusDetail());
}

void TrayController::activate(const QString& profileId) {
    const OperationResult result = m_controller.activateProfile(profileId);
    updateAppearance();
    notify(QStringLiteral("ControlD Tray"), result.notification);
}

void TrayController::disable() {
    const OperationResult result = m_controller.disable();
    updateAppearance();
    notify(QStringLiteral("ControlD Tray"), result.notification);
}

void TrayController::flush() {
    const OperationResult result = m_controller.flush();
    updateAppearance();
    notify(QStringLiteral("ControlD Tray"), result.notification);
}

void TrayController::restartChrome() {
    const QList<ProcessImage> live = ChromeProcesses::scan();
    const RestartPlan plan = ChromeProcesses::plan(live);
    if (!plan.refusal.isEmpty()) {
        QMessageBox::information(
            &m_settings, QStringLiteral("Restart Chrome"),
            QStringLiteral(
                "Chrome is not running. Its hostname cache is empty the next time it starts."));
        return;
    }
    const QString question =
        QStringLiteral(
            "Quit %1 browser process(es)?\n\n%2\n\n"
            "Chrome's hostname cache cannot be flushed any other way. Unsaved pages are restored "
            "only if the browser is set to reopen them.")
            .arg(plan.pids.size())
            .arg(plan.executables.join(QStringLiteral("\n")));
    if (QMessageBox::question(&m_settings, QStringLiteral("Restart Chrome"), question) !=
        QMessageBox::Yes) {
        return;
    }

    const QList<qint64> pids = ChromeProcesses::signalablePids(plan.pids, ChromeProcesses::scan(),
                                                               QCoreApplication::applicationPid());
    if (pids.isEmpty()) {
        QMessageBox::information(&m_settings, QStringLiteral("Restart Chrome"),
                                 QStringLiteral("The browser exited before it could be signaled."));
        return;
    }

    QStringList errors;
    for (const qint64 pid : pids) {
        QString error;
        if (!ChromeProcesses::signalPid(pid, SIGTERM, &error)) {
            errors.append(QStringLiteral("%1: %2").arg(pid).arg(error));
        }
    }

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 2000) {
        const QList<qint64> stillThere = ChromeProcesses::signalablePids(
            pids, ChromeProcesses::scan(), QCoreApplication::applicationPid());
        if (stillThere.isEmpty()) {
            break;
        }
        QThread::msleep(50);
    }
    const QList<qint64> remaining = ChromeProcesses::signalablePids(
        pids, ChromeProcesses::scan(), QCoreApplication::applicationPid());
    if (!errors.isEmpty() || !remaining.isEmpty()) {
        QString text = QStringLiteral("Some browser processes are still running.");
        if (!remaining.isEmpty()) {
            QStringList ids;
            for (const qint64 pid : remaining) {
                ids.append(QString::number(pid));
            }
            text += QStringLiteral(" Still running: %1.").arg(ids.join(QStringLiteral(", ")));
        }
        if (!errors.isEmpty()) {
            text += QStringLiteral("\n") + errors.join(QStringLiteral("\n"));
        }
        QMessageBox::warning(&m_settings, QStringLiteral("Restart Chrome"), text);
        return;
    }
    QMessageBox::information(
        &m_settings, QStringLiteral("Restart Chrome"),
        QStringLiteral(
            "Chrome was asked to quit. Start it again when you want the new DNS policy."));
}

} // namespace controld
