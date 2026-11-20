#pragma once

#include "core/ProfileController.hpp"

#include <QObject>
#include <QSystemTrayIcon>

class QMenu;

namespace controld {

class SettingsWindow;

class TrayController : public QObject {
    Q_OBJECT

  public:
    TrayController(ProfileController& controller, SettingsWindow& settings,
                   QObject* parent = nullptr);

    void notify(const QString& title, const QString& body);

  private slots:
    void rebuildMenu();
    void updateAppearance();
    void activate(const QString& profileId);
    void disable();
    void flush();
    void restartChrome();

  private:
    ProfileController& m_controller;
    SettingsWindow& m_settings;
    QSystemTrayIcon m_tray;
    QMenu* m_menu = nullptr;
};

} // namespace controld
