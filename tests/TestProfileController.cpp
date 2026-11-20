#include "Mocks.hpp"
#include "core/ChromePolicyHelper.hpp"
#include "core/ControlD.hpp"
#include "core/ProfileController.hpp"
#include "core/Verification.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class TestProfileController : public QObject {
    Q_OBJECT

  private slots:
    void activateConfiguresResolvedAndVerifies();
    void verificationFailureIsNotReportedActive();
    void dnsErrorStopsBeforeFlush();
    void disableRevertsOnlyLinksThisAppConfigured();
    void disableKeepsTheLinkWhenDnsRestoreFails();
    void linkSwitchRevertsThePreviousInterface();
    void disappearedPreviousLinkIsNotFatal();
    void chromePolicyIsPinnedOnceAndClearedOnDisable();
    void chromeWriteFailureDoesNotHideVerifiedDns();
    void flushRecheckUsesTheLiveAnswer();
    void flushCannotPromoteAFailedSwitch();
    void partialChromeApplyIsCleanedAfterRestart();
    void persistenceFailureStopsSystemChanges();
    void activationOnSameInterfaceReapplies();
    void failedBookkeepingPreservesChromeCleanupIntent();
};

struct Fixture {
    QTemporaryDir dir;
    controld::ProfileStore store;
    controld::MockResolved resolved;
    controld::MockLink link;
    controld::MockChrome chrome;
    controld::ProfileController controller;

    Fixture()
        : store(dir.filePath(QStringLiteral("settings.ini"))),
          controller(store, resolved, link, chrome) {}

    QString add(const QString& name, const QString& resolverId) {
        QString error;
        const QString id = controller.addProfile(name, resolverId, &error);
        Q_ASSERT(!id.isEmpty());
        return id;
    }
};

void TestProfileController::activateConfiguresResolvedAndVerifies() {
    Fixture fixture;
    QVERIFY(fixture.dir.isValid());
    const QString id = fixture.add(QStringLiteral("Home"), QStringLiteral("abcd1234"));
    const controld::OperationResult result = fixture.controller.activateProfile(id);
    QVERIFY(result.ok);
    QCOMPARE(result.status, controld::RuntimeStatus::Active);
    QVERIFY(result.notification.contains(QStringLiteral("cannot flush")));
    QCOMPARE(fixture.controller.statusLine(), QStringLiteral("Active: Home"));
    QCOMPARE(fixture.store.appliedIfindexes(), QList<int>({3}));

    const QStringList methods = [&]() {
        QStringList names;
        for (const auto& call : fixture.resolved.calls) {
            names.append(call.method);
        }
        return names;
    }();
    QCOMPARE(methods,
             QStringList({QStringLiteral("setLinkDnsEx"), QStringLiteral("setLinkDnsOverTls"),
                          QStringLiteral("setLinkDomains"), QStringLiteral("setLinkDefaultRoute"),
                          QStringLiteral("flushCaches"), QStringLiteral("resolveHostname")}));

    const auto& dns = fixture.resolved.calls.at(0);
    QCOMPARE(dns.ifindex, 3);
    QString error;
    QCOMPARE(dns.servers, controld::ControlD::serversFor(QStringLiteral("abcd1234"), &error));
    QCOMPARE(fixture.resolved.calls.at(1).text, QStringLiteral("yes"));
    QCOMPARE(fixture.resolved.calls.at(2).domains,
             QList<controld::SearchDomain>({controld::SearchDomain(QStringLiteral("."), true)}));
    QVERIFY(fixture.resolved.calls.at(3).flag);
    QCOMPARE(fixture.resolved.calls.at(5).text, QStringLiteral("verify.controld.com"));
    QCOMPARE(fixture.resolved.calls.at(5).flags, controld::Verification::queryFlags());
    QVERIFY(fixture.chrome.applied.isEmpty());
}

void TestProfileController::verificationFailureIsNotReportedActive() {
    Fixture fixture;
    const QString id = fixture.add(QStringLiteral("Home"), QStringLiteral("abcd1234"));
    fixture.resolved.nextResolve.canonicalName = QStringLiteral("example.com");
    const controld::OperationResult result = fixture.controller.activateProfile(id);
    QVERIFY(!result.ok);
    QCOMPARE(result.status, controld::RuntimeStatus::Failed);
    QVERIFY(result.detail.contains(QStringLiteral("example.com")));
    QVERIFY(fixture.chrome.applied.isEmpty());
    QCOMPARE(fixture.controller.statusLine(), QStringLiteral("Failed: Home"));
}

void TestProfileController::dnsErrorStopsBeforeFlush() {
    Fixture fixture;
    const QString id = fixture.add(QStringLiteral("Home"), QStringLiteral("abcd1234"));
    fixture.resolved.failures.insert(
        QStringLiteral("setLinkDnsEx"),
        controld::CallResult::failure(QStringLiteral("org.freedesktop.DBus.Error.AccessDenied"),
                                      QStringLiteral("canceled")));
    const controld::OperationResult result = fixture.controller.activateProfile(id);
    QVERIFY(!result.ok);
    QCOMPARE(result.status, controld::RuntimeStatus::Failed);
    QCOMPARE(fixture.resolved.calls.size(), 1);
    QVERIFY(fixture.store.appliedIfindexes().isEmpty());
}

void TestProfileController::disableRevertsOnlyLinksThisAppConfigured() {
    Fixture fixture;
    const controld::OperationResult idle = fixture.controller.disable();
    QVERIFY(idle.ok);
    QVERIFY(fixture.resolved.calls.isEmpty());
    QVERIFY(idle.detail.contains(QStringLiteral("were not changed")));

    const QString id = fixture.add(QStringLiteral("Home"), QStringLiteral("abcd1234"));
    QVERIFY(fixture.controller.activateProfile(id).ok);
    fixture.resolved.calls.clear();
    const controld::OperationResult disabled = fixture.controller.disable();
    QVERIFY(disabled.ok);
    QCOMPARE(disabled.status, controld::RuntimeStatus::Disabled);
    QCOMPARE(fixture.resolved.calls.at(0).method, QStringLiteral("revertLink"));
    QCOMPARE(fixture.resolved.calls.at(0).ifindex, 3);
    QCOMPARE(fixture.resolved.calls.at(1).method, QStringLiteral("setLinkDnsEx"));
    QCOMPARE(fixture.resolved.calls.at(1).ifindex, 3);
    QCOMPARE(fixture.resolved.calls.at(1).servers.size(), 1);
    QCOMPARE(fixture.resolved.calls.at(1).servers.at(0).address,
             QHostAddress(QStringLiteral("192.168.1.1")));
    QVERIFY(fixture.resolved.calls.at(1).servers.at(0).sni.isEmpty());
    QCOMPARE(fixture.resolved.calls.at(2).method, QStringLiteral("setLinkDefaultRoute"));
    QVERIFY(fixture.resolved.calls.at(2).flag);
    QCOMPARE(fixture.resolved.calls.at(3).method, QStringLiteral("flushCaches"));
    QVERIFY(fixture.store.appliedIfindexes().isEmpty());
    QVERIFY(fixture.store.activeProfileId().isEmpty());
    QVERIFY(disabled.detail.contains(QStringLiteral("written back")));
    QVERIFY(disabled.detail.contains(QStringLiteral("wlan0")));
}

void TestProfileController::disableKeepsTheLinkWhenDnsRestoreFails() {
    Fixture fixture;
    const QString id = fixture.add(QStringLiteral("Home"), QStringLiteral("abcd1234"));
    QVERIFY(fixture.controller.activateProfile(id).ok);
    fixture.resolved.calls.clear();
    fixture.resolved.failures.insert(
        QStringLiteral("setLinkDnsEx"),
        controld::CallResult::failure(QStringLiteral("org.freedesktop.DBus.Error.AccessDenied"),
                                      QStringLiteral("canceled")));
    const controld::OperationResult disabled = fixture.controller.disable();
    QVERIFY(!disabled.ok);
    QCOMPARE(disabled.status, controld::RuntimeStatus::Failed);
    QCOMPARE(fixture.store.appliedIfindexes(), QList<int>({3}));
    QVERIFY(disabled.detail.contains(QStringLiteral("NetworkManager DNS")));
}

void TestProfileController::linkSwitchRevertsThePreviousInterface() {
    Fixture fixture;
    const QString id = fixture.add(QStringLiteral("Home"), QStringLiteral("abcd1234"));
    QVERIFY(fixture.controller.activateProfile(id).ok);
    fixture.resolved.calls.clear();
    fixture.link.index = 5;
    fixture.link.name = QStringLiteral("eth0");
    fixture.controller.onPrimaryLinkChanged();
    QCOMPARE(fixture.controller.status(), controld::RuntimeStatus::Active);
    QCOMPARE(fixture.store.appliedIfindexes(), QList<int>({5}));

    QStringList methods;
    QList<int> indexes;
    for (const auto& call : fixture.resolved.calls) {
        methods.append(call.method);
        indexes.append(call.ifindex);
    }
    QCOMPARE(methods.first(), QStringLiteral("setLinkDnsEx"));
    QCOMPARE(indexes.first(), 5);
    QVERIFY(methods.contains(QStringLiteral("revertLink")));
    QCOMPARE(fixture.resolved.calls.at(methods.indexOf(QStringLiteral("revertLink"))).ifindex, 3);
    QVERIFY(fixture.controller.startup().notification.isEmpty());
}

void TestProfileController::disappearedPreviousLinkIsNotFatal() {
    Fixture fixture;
    const QString id = fixture.add(QStringLiteral("Home"), QStringLiteral("abcd1234"));
    QVERIFY(fixture.controller.activateProfile(id).ok);
    fixture.link.index = 8;
    fixture.link.name = QStringLiteral("tun0");
    fixture.resolved.failRevertIfindex = 3;
    fixture.resolved.revertFailure = controld::CallResult::failure(
        controld::noSuchLinkErrorName(), QStringLiteral("Link 3 not known"));
    fixture.controller.onPrimaryLinkChanged();
    QVERIFY(fixture.controller.status() == controld::RuntimeStatus::Active);
    QCOMPARE(fixture.store.appliedIfindexes(), QList<int>({8}));
    QVERIFY(fixture.controller.statusDetail().contains(QStringLiteral("api.controld.com")));
}

void TestProfileController::chromePolicyIsPinnedOnceAndClearedOnDisable() {
    Fixture fixture;
    QVERIFY(fixture.controller.setChromePolicyEnabled(true).ok);
    const QString id = fixture.add(QStringLiteral("Home"), QStringLiteral("abcd1234"));
    const controld::OperationResult first = fixture.controller.activateProfile(id);
    QVERIFY(first.ok);
    QVERIFY(first.suggestChromeRestart);
    QCOMPARE(fixture.chrome.applied, QStringList({QStringLiteral("abcd1234")}));
    QCOMPARE(fixture.store.chromePinnedResolverId(), QStringLiteral("abcd1234"));

    fixture.resolved.calls.clear();
    const controld::OperationResult second = fixture.controller.activateProfile(id);
    QVERIFY(second.ok);
    QCOMPARE(fixture.chrome.applied.size(), 1);

    const controld::OperationResult disabled = fixture.controller.disable();
    QVERIFY(disabled.ok);
    QCOMPARE(fixture.chrome.clears, 1);
    QVERIFY(fixture.store.chromePinnedResolverId().isEmpty());
    QVERIFY(fixture.store.chromePolicyEnabled());
}

void TestProfileController::chromeWriteFailureDoesNotHideVerifiedDns() {
    Fixture fixture;
    QVERIFY(fixture.controller.setChromePolicyEnabled(true).ok);
    fixture.chrome.applyResult =
        controld::CallResult::failure({}, QStringLiteral("polkit dismissed"));
    const QString id = fixture.add(QStringLiteral("Home"), QStringLiteral("abcd1234"));
    const controld::OperationResult result = fixture.controller.activateProfile(id);
    QVERIFY(!result.ok);
    QCOMPARE(result.status, controld::RuntimeStatus::Active);
    QVERIFY(result.detail.contains(QStringLiteral("polkit dismissed")));
    QVERIFY(fixture.store.chromePinnedResolverId().isEmpty());
}

void TestProfileController::flushRecheckUsesTheLiveAnswer() {
    Fixture fixture;
    const QString id = fixture.add(QStringLiteral("Home"), QStringLiteral("abcd1234"));
    QVERIFY(fixture.controller.activateProfile(id).ok);
    fixture.resolved.nextResolve.ok = false;
    fixture.resolved.nextResolve.errorMessage = QStringLiteral("timed out");
    const controld::OperationResult flushed = fixture.controller.flush();
    QVERIFY(!flushed.ok);
    QCOMPARE(fixture.controller.status(), controld::RuntimeStatus::Failed);
    QVERIFY(flushed.notification.contains(QStringLiteral("timed out")));
    QVERIFY(flushed.notification.contains(QStringLiteral("cannot flush")));
}

void TestProfileController::flushCannotPromoteAFailedSwitch() {
    Fixture fixture;
    const QString a = fixture.add(QStringLiteral("A"), QStringLiteral("aaaa"));
    const QString b = fixture.add(QStringLiteral("B"), QStringLiteral("bbbb"));
    QVERIFY(fixture.controller.activateProfile(a).ok);
    fixture.resolved.failures.insert(QStringLiteral("setLinkDnsEx"),
                                     controld::CallResult::failure({}, QStringLiteral("denied")));
    QVERIFY(!fixture.controller.activateProfile(b).ok);
    fixture.resolved.calls.clear();
    QVERIFY(!fixture.controller.flush().ok);
    QCOMPARE(fixture.controller.status(), controld::RuntimeStatus::Failed);
    QCOMPARE(fixture.resolved.calls.size(), 1);
    QVERIFY(fixture.controller.activateProfile(b).ok);
    QVERIFY(fixture.controller.flush().ok);
}

void TestProfileController::partialChromeApplyIsCleanedAfterRestart() {
    class DiskChrome final : public controld::IChromePolicyWriter {
      public:
        QStringList paths;
        int clears = 0;
        controld::CallResult apply(const QString& id) override {
            const auto result =
                controld::runChromePolicyHelper({QStringLiteral("apply"), id}, paths);
            return result.code == 0 ? controld::CallResult::success()
                                    : controld::CallResult::failure({}, result.error);
        }
        controld::CallResult clear() override {
            ++clears;
            const auto result = controld::runChromePolicyHelper({QStringLiteral("clear")}, paths);
            return result.code == 0 ? controld::CallResult::success()
                                    : controld::CallResult::failure({}, result.error);
        }
    } chrome;
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("settings.ini"));
    const QString first = dir.filePath(QStringLiteral("chrome/controld.json"));
    const QString second = dir.filePath(QStringLiteral("chromium/controld.json"));
    QVERIFY(QDir().mkpath(second));
    chrome.paths = {first, second};
    controld::MockResolved resolved;
    controld::MockLink link;
    {
        controld::ProfileStore store(path);
        controld::ProfileController controller(store, resolved, link, chrome);
        const QString id =
            controller.addProfile(QStringLiteral("Home"), QStringLiteral("aaaa"), nullptr);
        QVERIFY(controller.setChromePolicyEnabled(true).ok);
        QVERIFY(!controller.activateProfile(id).ok);
        QVERIFY(QFile::exists(first));
        QVERIFY(store.chromePolicyDirty());
        QVERIFY(!controller.disable().ok); // second destination still cannot be cleared
        QVERIFY(store.chromePolicyDirty());
    }
    QVERIFY(QDir().rmdir(second));
    controld::ProfileStore reloaded(path);
    QVERIFY(reloaded.chromePolicyDirty());
    controld::ProfileController restarted(reloaded, resolved, link, chrome);
    QVERIFY(restarted.startup().ok);
    QVERIFY(!QFile::exists(first));
    QVERIFY(!reloaded.chromePolicyDirty());
    QVERIFY(reloaded.chromePinnedResolverId().isEmpty());
    QCOMPARE(chrome.clears, 2);
}

void TestProfileController::persistenceFailureStopsSystemChanges() {
    Fixture fixture;
    const QString id = fixture.add(QStringLiteral("Home"), QStringLiteral("aaaa"));
    const QString path = fixture.store.filePath();
    QVERIFY(QFile::remove(path));
    QVERIFY(QDir().mkdir(path));
    const auto result = fixture.controller.activateProfile(id);
    QVERIFY(!result.ok);
    QVERIFY(result.notification.contains(QStringLiteral("save settings")));
    QVERIFY(fixture.resolved.calls.isEmpty());
    QVERIFY(!fixture.controller.setChromePolicyEnabled(true).ok);
    QVERIFY(fixture.chrome.applied.isEmpty());
}

void TestProfileController::activationOnSameInterfaceReapplies() {
    Fixture fixture;
    const QString id = fixture.add(QStringLiteral("Home"), QStringLiteral("aaaa"));
    QVERIFY(fixture.controller.activateProfile(id).ok);
    fixture.resolved.calls.clear();
    fixture.controller.onPrimaryLinkActivated();
    QVERIFY(!fixture.resolved.calls.isEmpty());
    QCOMPARE(fixture.resolved.calls.first().method, QStringLiteral("setLinkDnsEx"));
}

void TestProfileController::failedBookkeepingPreservesChromeCleanupIntent() {
    class FailingStorageChrome final : public controld::IChromePolicyWriter {
      public:
        QString settings;
        controld::CallResult apply(const QString& id) override {
            Q_UNUSED(id);
            // Simulate storage becoming unavailable after the privileged write.
            if (!QFile::rename(settings, settings + QStringLiteral(".saved")) ||
                !QDir().mkdir(settings)) {
                return controld::CallResult::failure({}, QStringLiteral("fixture failed"));
            }
            return controld::CallResult::success();
        }
        controld::CallResult clear() override {
            return controld::CallResult::success();
        }
    } chrome;
    QTemporaryDir dir;
    chrome.settings = dir.filePath(QStringLiteral("settings.ini"));
    controld::ProfileStore store(chrome.settings);
    controld::MockResolved resolved;
    controld::MockLink link;
    controld::ProfileController controller(store, resolved, link, chrome);
    const QString id =
        controller.addProfile(QStringLiteral("Home"), QStringLiteral("aaaa"), nullptr);
    QVERIFY(controller.setChromePolicyEnabled(true).ok);
    const auto applied = controller.activateProfile(id);
    QVERIFY(!applied.ok);
    QVERIFY(applied.detail.contains(QStringLiteral("save settings")));
    QVERIFY(store.chromePolicyDirty());
    controld::ProfileStore persisted(chrome.settings + QStringLiteral(".saved"));
    QVERIFY(persisted.chromePolicyDirty());
    QVERIFY(persisted.chromePinnedResolverId().isEmpty());
}

QTEST_MAIN(TestProfileController)
#include "TestProfileController.moc"
