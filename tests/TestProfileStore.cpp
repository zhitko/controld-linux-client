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

QTEST_MAIN(TestProfileStore)
#include "TestProfileStore.moc"
