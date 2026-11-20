#include "core/ControlD.hpp"
#include "core/Validation.hpp"
#include "core/Verification.hpp"

#include <QTest>

class TestValidation : public QObject {
    Q_OBJECT

  private slots:
    void resolverIdAcceptsAsciiAlphanumeric();
    void resolverIdRejectsEverythingElse();
    void profileNameRules();
    void controldServersUseDocumentedAnycast();
    void verificationMatchesCanonicalNameOnly();
};

void TestValidation::resolverIdAcceptsAsciiAlphanumeric() {
    QVERIFY(controld::isValidResolverId(QStringLiteral("abc123")));
    QVERIFY(controld::isValidResolverId(QStringLiteral("ABC")));
    QVERIFY(controld::isValidResolverId(QString(64, QLatin1Char('a'))));
}

void TestValidation::resolverIdRejectsEverythingElse() {
    QVERIFY(!controld::isValidResolverId({}));
    QVERIFY(!controld::isValidResolverId(QStringLiteral("abc-def")));
    QVERIFY(!controld::isValidResolverId(QStringLiteral("abc.def")));
    QVERIFY(!controld::isValidResolverId(QStringLiteral("abc def")));
    QVERIFY(!controld::isValidResolverId(QStringLiteral("abc ")));
    QVERIFY(controld::isValidResolverId(QStringLiteral("p2")));
    QVERIFY(!controld::isValidResolverId(QStringLiteral("x-oisd")));
    QVERIFY(!controld::isValidResolverId(QStringLiteral("ab_cd")));
    QVERIFY(!controld::isValidResolverId(QString(65, QLatin1Char('a'))));
    QVERIFY(!controld::isValidResolverId(QStringLiteral("café")));
    QVERIFY(!controld::resolverIdError(QStringLiteral("bad-id")).isEmpty());
}

void TestValidation::profileNameRules() {
    QCOMPARE(controld::normalizedProfileName(QStringLiteral("  Home  ")), QStringLiteral("Home"));
    QVERIFY(controld::profileNameError(QStringLiteral("   ")).size() > 0);
    QVERIFY(controld::profileNameError(QStringLiteral("Home office")).isEmpty());
    QVERIFY(!controld::profileNameError(QStringLiteral("line\nbreak")).isEmpty());
    QVERIFY(controld::profileNameError(QString(80, QLatin1Char('a'))).isEmpty());
    QVERIFY(!controld::profileNameError(QString(81, QLatin1Char('a'))).isEmpty());
}

void TestValidation::controldServersUseDocumentedAnycast() {
    QString error;
    const auto servers = controld::ControlD::serversFor(QStringLiteral("abcd1234"), &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(servers.size(), 4);
    QCOMPARE(servers.at(0).address, QHostAddress(QStringLiteral("76.76.2.22")));
    QCOMPARE(servers.at(1).address, QHostAddress(QStringLiteral("76.76.10.22")));
    QCOMPARE(servers.at(2).address, QHostAddress(QStringLiteral("2606:1a40::22")));
    QCOMPARE(servers.at(3).address, QHostAddress(QStringLiteral("2606:1a40:1::22")));
    for (const auto& server : servers) {
        QCOMPARE(server.port, quint16(0));
        QCOMPARE(server.sni, QStringLiteral("abcd1234.dns.controld.com"));
    }
    QCOMPARE(controld::ControlD::dohTemplate(QStringLiteral("abcd1234")),
             QStringLiteral("https://dns.controld.com/abcd1234"));
    QVERIFY(controld::ControlD::serversFor(QStringLiteral("no-dashes"), &error).isEmpty());
    QCOMPARE(error, controld::resolverIdError(QStringLiteral("no-dashes")));
}

void TestValidation::verificationMatchesCanonicalNameOnly() {
    controld::ResolveResult result;
    result.ok = true;
    result.canonicalName = QStringLiteral("API.ControlD.com.");
    result.addresses.append(QHostAddress(QStringLiteral("147.185.34.1")));
    QVERIFY(controld::Verification::passed(result));

    result.canonicalName = QStringLiteral("verify.controld.com");
    QVERIFY(!controld::Verification::passed(result));
    QVERIFY(
        controld::Verification::failureDetail(result).contains(QStringLiteral("api.controld.com")));

    result.ok = false;
    result.errorName = QStringLiteral("org.freedesktop.DBus.Error.Failed");
    result.errorMessage = QStringLiteral("no answer");
    QVERIFY(controld::Verification::failureDetail(result).contains(QStringLiteral("no answer")));

    result.ok = true;
    result.canonicalName = QStringLiteral("api.controld.com");
    result.addresses.clear();
    QVERIFY(!controld::Verification::passed(result));

    const quint64 flags = controld::Verification::queryFlags();
    QVERIFY(flags & (1ULL << 0));
    QVERIFY(flags & (1ULL << 8));
    QVERIFY(flags & (1ULL << 12));
}

QTEST_MAIN(TestValidation)
#include "TestValidation.moc"
