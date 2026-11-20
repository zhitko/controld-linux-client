#pragma once

#include "core/ProfileController.hpp"

#include <QDialog>

class QCheckBox;
class QLabel;
class QListWidget;
class QPushButton;

namespace controld {

class SettingsWindow : public QDialog {
    Q_OBJECT

  public:
    SettingsWindow(ProfileController& controller, QWidget* parent = nullptr);

  private slots:
    void reloadProfiles();
    void refreshStatus();
    void addProfile();
    void editProfile();
    void removeProfile();
    void activateSelected();
    void disableDns();
    void refreshHelperSection();
    void installHelper();
    void uninstallHelper();

  private:
    QString selectedProfileId() const;
    void showResult(const OperationResult& result);

    ProfileController& m_controller;
    QLabel* m_status = nullptr;
    QListWidget* m_profiles = nullptr;
    QCheckBox* m_chrome = nullptr;
    QCheckBox* m_autostart = nullptr;
    QLabel* m_helperNote = nullptr;
    QPushButton* m_installHelper = nullptr;
    QPushButton* m_uninstallHelper = nullptr;
};

} // namespace controld
