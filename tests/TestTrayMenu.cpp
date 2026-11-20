#include "Mocks.hpp"
#include "core/ProfileController.hpp"
#include "ui/SettingsWindow.hpp"
#include "ui/TrayController.hpp"

#include <QAction>
#include <QApplication>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QWidget>

class TestTrayMenu : public QObject {
    Q_OBJECT

  private slots:
    void menuIsFilledBeforeTheIconIsShown();
    void leftClickDoesNotOpenSettings();
    void settingsCanActivateAndDisableTheSelectedProfile();
    void fallbackWindowQuitsWhenClosed();
};

struct Fixture {
    QTemporaryDir dir;
    controld::ProfileStore store;
    controld::MockResolved resolved;
    controld::MockLink link;
    controld::MockChrome chrome;
    controld::ProfileController controller;
    controld::SettingsWindow settings;
    controld::TrayController tray;

    Fixture()
        : store(dir.filePath(QStringLiteral("settings.ini"))),
          controller(store, resolved, link, chrome), settings(controller),
          tray(controller, settings) {}

    QString add(const QString& name, const QString& resolverId) {
        QString error;
        const QString id = controller.addProfile(name, resolverId, &error);
        Q_ASSERT(!id.isEmpty());
        return id;
    }
};

QStringList actionLabels(const QMenu* menu) {
    QStringList labels;
    for (QAction* action : menu->actions()) {
        if (!action->isSeparator()) {
            labels.append(action->text());
        }
    }
    return labels;
}

void dismissModal() {
    QTimer::singleShot(0, []() {
        QWidget* box = QApplication::activeModalWidget();
        if (box != nullptr) {
            box->close();
        }
    });
}

void TestTrayMenu::menuIsFilledBeforeTheIconIsShown() {
    Fixture fixture;
    QVERIFY(fixture.dir.isValid());
    QVERIFY(fixture.tray.menuPreparedBeforeShow());

    QMenu* menu = fixture.tray.contextMenu();
    QVERIFY(menu != nullptr);
    QVERIFY(!menu->actions().isEmpty());
    QCOMPARE(actionLabels(menu),
             QStringList({QStringLiteral("Off"), QStringLiteral("Disable (restore default DNS)"),
                          QStringLiteral("Flush DNS cache"), QStringLiteral("Settings"),
                          QStringLiteral("Restart Chrome"), QStringLiteral("Quit")}));

    QAction* disableAction = nullptr;
    for (QAction* action : menu->actions()) {
        if (action->text() == QStringLiteral("Disable (restore default DNS)")) {
            disableAction = action;
        }
    }
    QVERIFY(disableAction != nullptr);
    QVERIFY(disableAction->isChecked());

    const QString id = fixture.add(QStringLiteral("Home"), QStringLiteral("abcd1234"));
    QVERIFY(!id.isEmpty());
    QCOMPARE(fixture.tray.contextMenu(), menu);
    QCOMPARE(actionLabels(menu),
             QStringList({QStringLiteral("Off"), QStringLiteral("Home (abcd1234)"),
                          QStringLiteral("Disable (restore default DNS)"),
                          QStringLiteral("Flush DNS cache"), QStringLiteral("Settings"),
                          QStringLiteral("Restart Chrome"), QStringLiteral("Quit")}));
}

void TestTrayMenu::leftClickDoesNotOpenSettings() {
    Fixture fixture;
    QVERIFY(fixture.dir.isValid());
    QVERIFY(!fixture.settings.isVisible());

    fixture.tray.handleActivation(QSystemTrayIcon::Trigger);
    QVERIFY(!fixture.settings.isVisible());
    fixture.tray.contextMenu()->hide();

    fixture.tray.handleActivation(QSystemTrayIcon::DoubleClick);
    QVERIFY(!fixture.settings.isVisible());
    fixture.tray.contextMenu()->hide();

    fixture.tray.handleActivation(QSystemTrayIcon::Context);
    QVERIFY(!fixture.settings.isVisible());
    QVERIFY(!fixture.tray.contextMenu()->isVisible());
}

void TestTrayMenu::settingsCanActivateAndDisableTheSelectedProfile() {
    Fixture fixture;
    QVERIFY(fixture.dir.isValid());
    const QString id = fixture.add(QStringLiteral("Home"), QStringLiteral("abcd1234"));

    auto* profiles = fixture.settings.findChild<QListWidget*>();
    QVERIFY(profiles != nullptr);
    QCOMPARE(profiles->count(), 1);
    profiles->setCurrentRow(0);

    bool sawUse = false;
    bool sawDisable = false;
    const auto buttons = fixture.settings.findChildren<QPushButton*>();
    for (QPushButton* button : buttons) {
        if (button->text() == QStringLiteral("Use this profile")) {
            sawUse = true;
        }
        if (button->text() == QStringLiteral("Disable (restore default DNS)")) {
            sawDisable = true;
        }
    }
    QVERIFY(sawUse);
    QVERIFY(sawDisable);

    dismissModal();
    QVERIFY(QMetaObject::invokeMethod(&fixture.settings, "activateSelected"));
    QCOMPARE(fixture.store.activeProfileId(), id);
    QCOMPARE(profiles->item(0)->text(), QStringLiteral("Home (abcd1234) — active"));
    QCOMPARE(fixture.tray.contextMenu()->actions().at(2)->text(),
             QStringLiteral("Home (abcd1234)"));
    QVERIFY(fixture.tray.contextMenu()->actions().at(2)->isChecked());

    dismissModal();
    QVERIFY(QMetaObject::invokeMethod(&fixture.settings, "disableDns"));
    QVERIFY(fixture.store.activeProfileId().isEmpty());
    QCOMPARE(fixture.controller.status(), controld::RuntimeStatus::Disabled);
    QCOMPARE(profiles->item(0)->text(), QStringLiteral("Home (abcd1234)"));
}

void TestTrayMenu::fallbackWindowQuitsWhenClosed() {
    Fixture fixture;
    QApplication::setQuitOnLastWindowClosed(false);
    fixture.settings.showWithoutTray();
    QVERIFY(QApplication::quitOnLastWindowClosed());
    bool timedOut = false;
    QTimer watchdog;
    watchdog.setSingleShot(true);
    connect(&watchdog, &QTimer::timeout, qApp, [&]() {
        timedOut = true;
        QApplication::quit();
    });
    watchdog.start(1000);
    QTimer::singleShot(0, &fixture.settings, &QWidget::close);
    QApplication::exec();
    QVERIFY(!timedOut);
    QVERIFY(!fixture.settings.isVisible());
    QApplication::setQuitOnLastWindowClosed(false);
}

QTEST_MAIN(TestTrayMenu)
#include "TestTrayMenu.moc"
