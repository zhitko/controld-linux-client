#include "core/ControlD.hpp"
#include "core/Verification.hpp"
#include "dbus/ResolvedMarshalling.hpp"

#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QTest>
#include <QVariant>

#include <netinet/in.h>

class TestMarshalling : public QObject {
    Q_OBJECT

  private slots:
    void initTestCase();
    void dnsServersRoundTripWithResolvedSignature();
    void domainsDefaultRouteAndResolveSignatures();
    void decodeResolveAddresses();
};

void TestMarshalling::initTestCase() {
    controld::registerResolvedMetaTypes();
}

void TestMarshalling::dnsServersRoundTripWithResolvedSignature() {
    QString error;
    const auto servers = controld::ControlD::serversFor(QStringLiteral("abcd1234"), &error);
    QVERIFY(error.isEmpty());
    const auto wire = controld::serversToWire(servers, &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(wire.size(), 4);
    QCOMPARE(wire.at(0).family, AF_INET);
    QCOMPARE(wire.at(0).address, QByteArray::fromHex("4c4c0216"));
    QCOMPARE(wire.at(0).port, quint16(0));
    QCOMPARE(wire.at(0).sni, QStringLiteral("abcd1234.dns.controld.com"));
    QCOMPARE(wire.at(2).family, AF_INET6);
    QCOMPARE(wire.at(2).address.size(), 16);

    QCOMPARE(QString::fromLatin1(QDBusMetaType::typeToSignature(QMetaType::fromType<int>())),
             QStringLiteral("i"));
    QCOMPARE(QString::fromLatin1(QDBusMetaType::typeToSignature(
                 QMetaType::fromType<QList<controld::DnsServerExWire>>())),
             QStringLiteral("a(iayqs)"));

    int family = 0;
    const QByteArray ipv4 =
        controld::ipAddressToBytes(QHostAddress(QStringLiteral("76.76.2.22")), &family, &error);
    QCOMPARE(family, AF_INET);
    QCOMPARE(ipv4, QByteArray::fromHex("4c4c0216"));
    QCOMPARE(controld::ipAddressFromBytes(family, ipv4, &error),
             QHostAddress(QStringLiteral("76.76.2.22")));
    const QByteArray ipv6 =
        controld::ipAddressToBytes(QHostAddress(QStringLiteral("2606:1a40::22")), &family, &error);
    QCOMPARE(family, AF_INET6);
    QCOMPARE(controld::ipAddressFromBytes(family, ipv6, &error),
             QHostAddress(QStringLiteral("2606:1a40::22")));
    QVERIFY(error.isEmpty());

    controld::DnsServerEndpoint bad;
    QVERIFY(controld::serversToWire({bad}, &error).isEmpty());
    QVERIFY(!error.isEmpty());
}

void TestMarshalling::domainsDefaultRouteAndResolveSignatures() {
    QCOMPARE(QString::fromLatin1(QDBusMetaType::typeToSignature(
                 QMetaType::fromType<QList<controld::SearchDomainWire>>())),
             QStringLiteral("a(sb)"));
    QCOMPARE(QString::fromLatin1(QDBusMetaType::typeToSignature(QMetaType::fromType<QString>())),
             QStringLiteral("s"));
    QCOMPARE(QString::fromLatin1(QDBusMetaType::typeToSignature(QMetaType::fromType<bool>())),
             QStringLiteral("b"));
    QCOMPARE(QString::fromLatin1(QDBusMetaType::typeToSignature(QMetaType::fromType<quint64>())),
             QStringLiteral("t"));
    QCOMPARE(QString::fromLatin1(QDBusMetaType::typeToSignature(QMetaType::fromType<quint16>())),
             QStringLiteral("q"));

    const QList<controld::SearchDomainWire> domains =
        controld::domainsToWire({controld::SearchDomain(QStringLiteral("."), true)});
    QCOMPARE(domains.size(), 1);
    QCOMPARE(domains.at(0).name, QStringLiteral("."));
    QVERIFY(domains.at(0).routingOnly);

    QDBusMessage resolve = QDBusMessage::createMethodCall(
        QStringLiteral("org.example"), QStringLiteral("/org/example"),
        QStringLiteral("org.example"), QStringLiteral("ResolveHostname"));
    resolve << int(0) << QStringLiteral("verify.controld.com") << int(AF_UNSPEC)
            << controld::Verification::queryFlags();
    QCOMPARE(resolve.arguments().size(), 4);
    QCOMPARE(resolve.arguments().at(1).toString(), QStringLiteral("verify.controld.com"));
    QCOMPARE(resolve.arguments().at(3).toULongLong(), controld::Verification::queryFlags());
}

void TestMarshalling::decodeResolveAddresses() {
    QString error;
    QVERIFY(controld::ipAddressFromBytes(AF_INET, QByteArray::fromHex("4c"), &error).isNull());
    QVERIFY(error.contains(QStringLiteral("unexpected length")));
    QVERIFY(controld::ipAddressFromBytes(99, QByteArray::fromHex("4c4c0216"), &error).isNull());
    QVERIFY(error.contains(QStringLiteral("not IPv4 or IPv6")));
}

QTEST_MAIN(TestMarshalling)
#include "TestMarshalling.moc"
