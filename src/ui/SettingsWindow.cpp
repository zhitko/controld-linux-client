#include "ui/SettingsWindow.hpp"

#include "core/AutostartManager.hpp"
#include "ui/ProfileEditorDialog.hpp"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace controld {

SettingsWindow::SettingsWindow(ProfileController& controller, QWidget* parent)
    : QDialog(parent), m_controller(controller) {
    setWindowTitle(QStringLiteral("ControlD Tray"));
    setAttribute(Qt::WA_QuitOnClose, false);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_profiles = new QListWidget(this);
    connect(m_profiles, &QListWidget::itemDoubleClicked, this, &SettingsWindow::editProfile);

    auto* addButton = new QPushButton(QStringLiteral("Add"), this);
    auto* editButton = new QPushButton(QStringLiteral("Edit"), this);
    auto* removeButton = new QPushButton(QStringLiteral("Remove"), this);
    connect(addButton, &QPushButton::clicked, this, &SettingsWindow::addProfile);
    connect(editButton, &QPushButton::clicked, this, &SettingsWindow::editProfile);
    connect(removeButton, &QPushButton::clicked, this, &SettingsWindow::removeProfile);

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(addButton);
    buttons->addWidget(editButton);
    buttons->addWidget(removeButton);
    buttons->addStretch(1);

    m_chrome = new QCheckBox(
        QStringLiteral("Pin Chrome and Chromium Secure DNS to the active profile"), this);
    m_chrome->setChecked(m_controller.store().chromePolicyEnabled());
    auto* chromeNote = new QLabel(
        QStringLiteral("Off by default. When a profile is active this writes "
                       "/etc/opt/chrome/policies/managed/controld.json and "
                       "/etc/chromium/policies/managed/controld.json through a polkit prompt. "
                       "Chrome's hostname cache cannot be flushed from outside the browser."),
        this);
    chromeNote->setWordWrap(true);

    m_autostart = new QCheckBox(QStringLiteral("Start ControlD Tray when I log in"), this);
    m_autostart->setChecked(AutostartManager::isEnabled());

    connect(m_chrome, &QCheckBox::toggled, this, [this](bool checked) {
        const OperationResult result = m_controller.setChromePolicyEnabled(checked);
        if (!result.ok) {
            const QSignalBlocker blocker(m_chrome);
            m_chrome->setChecked(m_controller.store().chromePolicyEnabled());
        }
        showResult(result);
    });
    connect(m_autostart, &QCheckBox::toggled, this, [this](bool checked) {
        QString error;
        const bool ok =
            AutostartManager::setEnabled(checked, QCoreApplication::applicationFilePath(), &error);
        if (!ok) {
            const QSignalBlocker blocker(m_autostart);
            m_autostart->setChecked(AutostartManager::isEnabled());
            QMessageBox::warning(this, QStringLiteral("Autostart"), error);
        }
    });

    auto* closeButtons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(closeButtons, &QDialogButtonBox::rejected, this, &QDialog::close);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_status);
    layout->addWidget(m_profiles, 1);
    layout->addLayout(buttons);
    layout->addWidget(m_chrome);
    layout->addWidget(chromeNote);
    layout->addWidget(m_autostart);
    layout->addWidget(closeButtons);
    resize(520, 420);

    connect(&m_controller, &ProfileController::statusChanged, this, &SettingsWindow::refreshStatus);
    connect(&m_controller, &ProfileController::profilesChanged, this,
            &SettingsWindow::reloadProfiles);
    reloadProfiles();
    refreshStatus();
}

void SettingsWindow::reloadProfiles() {
    const QString selected = selectedProfileId();
    m_profiles->clear();
    for (const Profile& profile : m_controller.store().profiles()) {
        auto* item = new QListWidgetItem(
            QStringLiteral("%1 (%2)").arg(profile.name, profile.resolverId), m_profiles);
        item->setData(Qt::UserRole, profile.id);
        if (profile.id == selected) {
            m_profiles->setCurrentItem(item);
        }
    }
}

void SettingsWindow::refreshStatus() {
    m_status->setText(m_controller.statusLine() + QStringLiteral("\n") +
                      m_controller.statusDetail());
}

void SettingsWindow::addProfile() {
    ProfileEditorDialog dialog({}, {}, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    QString error;
    const QString id = m_controller.addProfile(dialog.profileName(), dialog.resolverId(), &error);
    if (id.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Profile"), error);
    }
}

void SettingsWindow::editProfile() {
    const QString id = selectedProfileId();
    if (id.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Profile"),
                                 QStringLiteral("Select a profile first."));
        return;
    }
    const std::optional<Profile> profile = m_controller.store().profile(id);
    if (!profile.has_value()) {
        return;
    }
    ProfileEditorDialog dialog(profile->name, profile->resolverId, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    showResult(m_controller.updateProfile(id, dialog.profileName(), dialog.resolverId()));
}

void SettingsWindow::removeProfile() {
    const QString id = selectedProfileId();
    if (id.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Profile"),
                                 QStringLiteral("Select a profile first."));
        return;
    }
    const std::optional<Profile> profile = m_controller.store().profile(id);
    if (!profile.has_value()) {
        return;
    }
    QString question = QStringLiteral("Remove profile \"%1\"?").arg(profile->name);
    if (id == m_controller.store().activeProfileId()) {
        question +=
            QStringLiteral(" It is the active profile, so its DNS settings will be reverted.");
    }
    if (QMessageBox::question(this, QStringLiteral("Remove profile"), question) !=
        QMessageBox::Yes) {
        return;
    }
    showResult(m_controller.removeProfile(id));
}

QString SettingsWindow::selectedProfileId() const {
    const QListWidgetItem* item = m_profiles->currentItem();
    if (item == nullptr) {
        return {};
    }
    return item->data(Qt::UserRole).toString();
}

void SettingsWindow::showResult(const OperationResult& result) {
    refreshStatus();
    if (!result.notification.isEmpty()) {
        if (result.ok) {
            QMessageBox::information(this, QStringLiteral("ControlD Tray"), result.notification);
        } else {
            QMessageBox::warning(this, QStringLiteral("ControlD Tray"), result.notification);
        }
    }
}

} // namespace controld
