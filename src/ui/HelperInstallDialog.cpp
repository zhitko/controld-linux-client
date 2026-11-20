#include "ui/HelperInstallDialog.hpp"

#include "core/HelperInstall.hpp"
#include "core/ProfileController.hpp"

#include <QMessageBox>
#include <QProcessEnvironment>

namespace controld {

void runHelperInstall(QWidget* parent) {
    HelperInstallSession session;
    QString error;
    if (!session.prepare(appImageLaunch(QProcessEnvironment::systemEnvironment()), &error)) {
        QMessageBox::warning(parent, QStringLiteral("Install helper"), error);
        return;
    }
    if (!privilegedCopyToolsReady(&error)) {
        QMessageBox::warning(parent, QStringLiteral("Install helper"), error);
        return;
    }
    if (QMessageBox::question(parent, QStringLiteral("Install helper"), session.prompt()) !=
        QMessageBox::Yes) {
        return;
    }
    if (!session.commit(&error)) {
        QMessageBox::warning(parent, QStringLiteral("Install helper"), error);
        return;
    }
    QMessageBox::information(
        parent, QStringLiteral("Install helper"),
        QStringLiteral("The Chrome policy helper is installed. DNS settings were not changed. "
                       "DNS changes now use a grouped authorization prompt. "
                       "You can now pin Chrome and Chromium Secure DNS from Settings."));
}

void runHelperUninstall(QWidget* parent, ProfileController& controller) {
    HelperUninstallSession session;
    QString error;
    if (!session.prepare(appImageLaunch(QProcessEnvironment::systemEnvironment()), &error)) {
        QMessageBox::warning(parent, QStringLiteral("Uninstall helper"), error);
        return;
    }
    if (!privilegedCopyToolsReady(&error)) {
        QMessageBox::warning(parent, QStringLiteral("Uninstall helper"), error);
        return;
    }
    if (QMessageBox::question(parent, QStringLiteral("Uninstall helper"), session.prompt()) !=
        QMessageBox::Yes) {
        return;
    }
    if (controller.store().chromePolicyEnabled() || controller.store().chromePolicyDirty() ||
        !controller.store().chromePinnedResolverId().isEmpty()) {
        const OperationResult cleared = controller.setChromePolicyEnabled(false);
        if (!cleared.ok) {
            QMessageBox::warning(parent, QStringLiteral("Uninstall helper"),
                                 cleared.notification.isEmpty()
                                     ? QStringLiteral("Could not remove the managed Chrome policy.")
                                     : cleared.notification);
            return;
        }
    }
    if (!session.commit(&error)) {
        QMessageBox::warning(parent, QStringLiteral("Uninstall helper"), error);
        return;
    }
    QMessageBox::information(
        parent, QStringLiteral("Uninstall helper"),
        QStringLiteral("The helper files that still matched this install were removed. "
                       "DNS settings were not changed."));
}

} // namespace controld
