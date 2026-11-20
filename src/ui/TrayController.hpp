#pragma once

#include "core/ProfileController.hpp"

#include <QObject>
#include <QSystemTrayIcon>
#include <QWidget>

class QMenu;

namespace controld {

class SettingsWindow;

class TrayController : public QObject {
    Q_OBJECT

  public:
    TrayController(ProfileController& controller, SettingsWindow& settings,
                   QObject* parent = nullptr);

    void notify(const QString& title, const QString& body);
    QMenu* contextMenu() const;
    bool menuPreparedBeforeShow() const;
    void handleActivation(QSystemTrayIcon::ActivationReason reason);

  private slots:
    void rebuildMenu();
    void updateAppearance();
    void activate(const QString& profileId);
    void disable();
    void flush();
    void restartChrome();

  private:
    void showSettings();

    ProfileController& m_controller;
    SettingsWindow& m_settings;
    QWidget m_menuOwner;
    QSystemTrayIcon m_tray;
    QMenu* m_menu = nullptr;
    bool m_menuPreparedBeforeShow = false;
};

} // namespace controld
