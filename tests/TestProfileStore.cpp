#include "core/ProfileStore.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

class TestProfileStore : public QObject {
    Q_OBJECT

  private slots:
    void roundTripAndRejectsInvalidIds();
    void fileIsOwnerReadWriteOnly();
    void failedWritesDoNotChangeMemoryOrReportSuccess();
    void readsExistingSerializedSettings();
};

void TestProfileStore::roundTripAndRejectsInvalidIds() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("settings.ini"));

    controld::ProfileStore store(path);
    QString error;
    QVERIFY(store.addProfile(QStringLiteral("  "), QStringLiteral("abc"), &error).isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY(store.addProfile(QStringLiteral("Home"), QStringLiteral("bad id"), &error).isEmpty());
    QVERIFY(store.addProfile(QStringLiteral("Home"), QStringLiteral("abc-def"), &error).isEmpty());

    const QString home =
        store.addProfile(QStringLiteral("  Home  "), QStringLiteral("abcd1234"), &error);
    QVERIFY(!home.isEmpty());
    const QString work =
        store.addProfile(QStringLiteral("Work"), QStringLiteral("ef567890"), &error);
    QVERIFY(!work.isEmpty());
    QVERIFY(store.updateProfile(home, QStringLiteral("House"), QStringLiteral("zzzz"), &error));
    QVERIFY(!store.updateProfile(home, QStringLiteral("House"), QStringLiteral("nope!"), &error));
    QCOMPARE(store.profile(home)->resolverId, QStringLiteral("zzzz"));

    store.setActiveProfileId(home);
    store.setAppliedIfindexes({2, 2, -1, 4});
    store.setChromePolicyEnabled(true);
    store.setChromePinnedResolverId(QStringLiteral("zzzz"));

    QVERIFY(store.removeProfile(work, &error));
    QCOMPARE(store.profiles().size(), 1);

    controld::ProfileStore reloaded(path);
    QCOMPARE(reloaded.profiles().size(), 1);
    QCOMPARE(reloaded.profiles().at(0).name, QStringLiteral("House"));
    QCOMPARE(reloaded.profiles().at(0).resolverId, QStringLiteral("zzzz"));
    QCOMPARE(reloaded.activeProfileId(), home);
    QCOMPARE(reloaded.appliedIfindexes(), QList<int>({2, 4}));
    QVERIFY(reloaded.chromePolicyEnabled());
    QCOMPARE(reloaded.chromePinnedResolverId(), QStringLiteral("zzzz"));

    QVERIFY(reloaded.removeProfile(home, &error));
    QVERIFY(reloaded.activeProfileId().isEmpty());
}

void TestProfileStore::fileIsOwnerReadWriteOnly() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("nested/settings.ini"));
    controld::ProfileStore store(path);
    QString error;
    QVERIFY(!store.addProfile(QStringLiteral("Home"), QStringLiteral("abcd"), &error).isEmpty());
    const QFileDevice::Permissions permissions = QFileInfo(path).permissions();
    QVERIFY(permissions & QFileDevice::ReadOwner);
    QVERIFY(permissions & QFileDevice::WriteOwner);
    QVERIFY((permissions & QFileDevice::ReadGroup) == 0);
    QVERIFY((permissions & QFileDevice::WriteGroup) == 0);
    QVERIFY((permissions & QFileDevice::ReadOther) == 0);
    QVERIFY((permissions & QFileDevice::WriteOther) == 0);
}

void TestProfileStore::failedWritesDoNotChangeMemoryOrReportSuccess() {
    QTemporaryDir dir;
    const QString parent = dir.filePath(QStringLiteral("config"));
    const QString path = parent + QStringLiteral("/settings.ini");
    controld::ProfileStore store(path);
    QString error;
    const QString id = store.addProfile(QStringLiteral("Home"), QStringLiteral("abcd"), &error);
    QVERIFY(!id.isEmpty());
    QVERIFY(store.setActiveProfileId(id, &error));
    QVERIFY(store.setChromePolicyDirty(true, &error));
    controld::ProfileStore reloaded(path);
    QVERIFY(reloaded.chromePolicyDirty());
    const auto profiles = store.profiles();
    QVERIFY(QDir().rename(parent, parent + QStringLiteral(".saved")));
    QFile blocker(parent);
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    blocker.close();

    QVERIFY(store.addProfile(QStringLiteral("Work"), QStringLiteral("efgh"), &error).isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY(!store.updateProfile(id, QStringLiteral("Changed"), QStringLiteral("efgh"), &error));
    QVERIFY(!store.removeProfile(id, &error));
    QCOMPARE(store.profiles(), profiles);
    QCOMPARE(store.activeProfileId(), id);
    QVERIFY(!store.setActiveProfileId({}, &error));
    QVERIFY(!store.setAppliedIfindexes({3}, &error));
    QVERIFY(!store.setChromePolicyEnabled(true, &error));
    QVERIFY(!store.setChromePinnedResolverId(QStringLiteral("abcd"), &error));
    QVERIFY(!store.setChromePolicyDirty(false, &error));
    QCOMPARE(store.activeProfileId(), id);
    QVERIFY(store.appliedIfindexes().isEmpty());
    QVERIFY(!store.chromePolicyEnabled());
    QVERIFY(store.chromePinnedResolverId().isEmpty());
    QVERIFY(store.chromePolicyDirty());
    QVERIFY(!error.isEmpty());
}

void TestProfileStore::readsExistingSerializedSettings() {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("settings.ini"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("[%General]\nactiveProfileId=home\nappliedIfindexes=3\n"
               "chromePinnedResolverId=aaaa\nchromePolicyEnabled=true\n"
               "[profiles]\n1\\id=home\n1\\name=Home\n1\\resolverId=aaaa\nsize=1\n");
    file.close();
    controld::ProfileStore store(path);
    QCOMPARE(store.activeProfileId(), QStringLiteral("home"));
    QCOMPARE(store.appliedIfindexes(), QList<int>({3}));
    QCOMPARE(store.chromePinnedResolverId(), QStringLiteral("aaaa"));
    QVERIFY(store.chromePolicyEnabled());
    QVERIFY(!store.chromePolicyDirty());
}

QTEST_MAIN(TestProfileStore)
#include "TestProfileStore.moc"
