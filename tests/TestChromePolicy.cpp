#include "core/ChromePolicy.hpp"
#include "core/ChromePolicyHelper.hpp"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

class TestChromePolicy : public QObject {
    Q_OBJECT

  private slots:
    void documentHasOnlySecureDnsKeys();
    void helperWritesAndClearsOnlyRequestedFiles();
    void helperRejectsBadIdsAndSymlinks();
    void managedPathsAreChromeAndChromium();
};

void TestChromePolicy::documentHasOnlySecureDnsKeys() {
    QString error;
    QVERIFY(controld::ChromePolicy::document(QStringLiteral("bad-id"), &error).isEmpty());
    QVERIFY(!error.isEmpty());

    const QByteArray json = controld::ChromePolicy::document(QStringLiteral("abc123"), &error);
    QVERIFY(error.isEmpty());
    const QJsonDocument parsed = QJsonDocument::fromJson(json);
    QVERIFY(parsed.isObject());
    QCOMPARE(parsed.object().size(), 2);
    QCOMPARE(parsed.object().value(QStringLiteral("DnsOverHttpsMode")).toString(),
             QStringLiteral("secure"));
    QCOMPARE(parsed.object().value(QStringLiteral("DnsOverHttpsTemplates")).toString(),
             QStringLiteral("https://dns.controld.com/abc123"));
}

void TestChromePolicy::helperWritesAndClearsOnlyRequestedFiles() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString chrome = dir.filePath(QStringLiteral("chrome/controld.json"));
    const QString chromium = dir.filePath(QStringLiteral("chromium/controld.json"));
    const controld::HelperOutcome wrote = controld::runChromePolicyHelper(
        {QStringLiteral("apply"), QStringLiteral("abc123")}, {chrome, chromium});
    QCOMPARE(wrote.code, 0);
    QVERIFY(wrote.error.isEmpty());

    QFile chromeFile(chrome);
    QVERIFY(chromeFile.open(QIODevice::ReadOnly));
    const QByteArray chromeBytes = chromeFile.readAll();
    QFile chromiumFile(chromium);
    QVERIFY(chromiumFile.open(QIODevice::ReadOnly));
    QCOMPARE(chromiumFile.readAll(), chromeBytes);
    QCOMPARE(QJsonDocument::fromJson(chromeBytes)
                 .object()
                 .value(QStringLiteral("DnsOverHttpsTemplates"))
                 .toString(),
             QStringLiteral("https://dns.controld.com/abc123"));

    const QString sibling = dir.filePath(QStringLiteral("chrome/other.json"));
    QVERIFY(!QFile::exists(sibling));

    const controld::HelperOutcome cleared =
        controld::runChromePolicyHelper({QStringLiteral("clear")}, {chrome, chromium});
    QCOMPARE(cleared.code, 0);
    QVERIFY(!QFile::exists(chrome));
    QVERIFY(!QFile::exists(chromium));

    const controld::HelperOutcome missing =
        controld::runChromePolicyHelper({QStringLiteral("clear")}, {chrome, chromium});
    QCOMPARE(missing.code, 0);
}

void TestChromePolicy::helperRejectsBadIdsAndSymlinks() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString target = dir.filePath(QStringLiteral("policy.json"));
    const controld::HelperOutcome bad = controld::runChromePolicyHelper(
        {QStringLiteral("apply"), QStringLiteral("abc def")}, {target});
    QCOMPARE(bad.code, 1);
    QVERIFY(!QFile::exists(target));

    const controld::HelperOutcome usage =
        controld::runChromePolicyHelper({QStringLiteral("nope")}, {target});
    QCOMPARE(usage.code, 1);

    const QString sentinelPath = dir.filePath(QStringLiteral("sentinel"));
    QFile sentinel(sentinelPath);
    QVERIFY(sentinel.open(QIODevice::WriteOnly));
    QCOMPARE(sentinel.write("secret"), qint64(6));
    sentinel.close();

    const QString linkPath = dir.filePath(QStringLiteral("linked.json"));
    QVERIFY(QFile::link(sentinelPath, linkPath));
    const controld::HelperOutcome linked = controld::runChromePolicyHelper(
        {QStringLiteral("apply"), QStringLiteral("abc123")}, {linkPath});
    QVERIFY(linked.code != 0);
    QFile reread(sentinelPath);
    QVERIFY(reread.open(QIODevice::ReadOnly));
    QCOMPARE(reread.readAll(), QByteArray("secret"));

    const controld::HelperOutcome relative =
        controld::runChromePolicyHelper({QStringLiteral("apply"), QStringLiteral("abc123")},
                                        {QStringLiteral("/tmp/../etc/passwd")});
    QCOMPARE(relative.code, 1);
}

void TestChromePolicy::managedPathsAreChromeAndChromium() {
    const QStringList paths = controld::ChromePolicy::managedPolicyPaths();
    QCOMPARE(paths, QStringList({QStringLiteral("/etc/opt/chrome/policies/managed/controld.json"),
                                 QStringLiteral("/etc/chromium/policies/managed/controld.json")}));
}

QTEST_MAIN(TestChromePolicy)
#include "TestChromePolicy.moc"
