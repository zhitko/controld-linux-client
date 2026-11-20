#include "dbus/NetworkManagerWatcher.hpp"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QProcess>
#include <QSignalSpy>
#include <QTest>

#include <cstdio>

namespace {
const QString service = QStringLiteral("org.freedesktop.NetworkManager");
const QString root = QStringLiteral("/org/freedesktop/NetworkManager");
const QString properties = QStringLiteral("org.freedesktop.DBus.Properties");
const QString activeInterface = service + QStringLiteral(".Connection.Active");

class FakeNetworkManager final : public QDBusVirtualObject {
  public:
    QString primary = root + QStringLiteral("/ActiveConnection/1");

    QString introspect(const QString& path) const override {
        Q_UNUSED(path);
        return {};
    }

    bool handleMessage(const QDBusMessage& message, const QDBusConnection& connection) override {
        if (message.member() == QLatin1String("Get") && message.arguments().size() == 2) {
            const QString property = message.arguments().at(1).toString();
            QVariant value;
            if (property == QLatin1String("PrimaryConnection")) {
                value = QVariant::fromValue(QDBusObjectPath(primary));
            } else if (property == QLatin1String("Devices")) {
                value = QVariant::fromValue(
                    QList<QDBusObjectPath> {QDBusObjectPath(root + QStringLiteral("/Devices/1"))});
            } else if (property == QLatin1String("State")) {
                value = uint(100);
            } else if (property == QLatin1String("IpInterface") ||
                       property == QLatin1String("Interface")) {
                value = QStringLiteral("lo");
            } else if (property == QLatin1String("Ip4Config")) {
                value = QVariant::fromValue(QDBusObjectPath(root + QStringLiteral("/IP4Config/1")));
            } else if (property == QLatin1String("Ip6Config")) {
                value = QVariant::fromValue(QDBusObjectPath(QStringLiteral("/")));
            } else if (property == QLatin1String("NameserverData")) {
                QVariantMap server;
                server.insert(QStringLiteral("address"), QStringLiteral("192.168.1.1"));
                value = QVariant::fromValue(QList<QVariantMap> {server});
            } else if (property == QLatin1String("DnsPriority")) {
                value = 100;
            } else if (property == QLatin1String("Searches")) {
                value = QStringList {QStringLiteral("lan")};
            } else {
                return false;
            }
            connection.send(message.createReply(QVariant::fromValue(QDBusVariant(value))));
            return true;
        }
        if (message.member() == QLatin1String("ChangePrimary")) {
            auto oldState = QDBusMessage::createSignal(primary, activeInterface,
                                                       QStringLiteral("StateChanged"));
            oldState << uint(4) << uint(0);
            connection.send(oldState);
            primary = root + QStringLiteral("/ActiveConnection/2");
            auto newState = QDBusMessage::createSignal(primary, activeInterface,
                                                       QStringLiteral("StateChanged"));
            newState << uint(2) << uint(0);
            connection.send(newState);
            auto changed =
                QDBusMessage::createSignal(root, properties, QStringLiteral("PropertiesChanged"));
            changed << service
                    << QVariantMap {{QStringLiteral("PrimaryConnection"),
                                     QVariant::fromValue(QDBusObjectPath(primary))}}
                    << QStringList {};
            connection.send(changed);
            connection.send(message.createReply());
            return true;
        }
        return false;
    }
};
} // namespace

class TestNetworkManagerWatcher : public QObject {
    Q_OBJECT
  private slots:
    void replacementOnSameInterfaceIsReapplied() {
        QProcess server;
        server.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--service")});
        QVERIFY(server.waitForStarted());
        QVERIFY(server.waitForReadyRead());
        QCOMPARE(server.readAllStandardOutput().trimmed(), QByteArray("ready"));
        controld::NetworkManagerWatcher watcher;
        QSignalSpy activated(&watcher, &controld::NetworkManagerWatcher::primaryLinkActivated);
        QSignalSpy changed(&watcher, &controld::NetworkManagerWatcher::primaryLinkChanged);
        watcher.start();
        QVERIFY(watcher.ifindex() > 0);
        const int originalIndex = watcher.ifindex();
        const auto request = QDBusMessage::createMethodCall(
            service, root, QStringLiteral("org.example.Test"), QStringLiteral("ChangePrimary"));
        QCOMPARE(QDBusConnection::systemBus().call(request).type(), QDBusMessage::ReplyMessage);
        QTRY_COMPARE(activated.size(), 1);
        QCOMPARE(watcher.ifindex(), originalIndex);
        QCOMPARE(changed.size(), 0);
        const controld::LinkDnsConfiguration dns = watcher.linkDns(originalIndex);
        QVERIFY(dns.error.isEmpty());
        QCOMPARE(dns.servers.size(), 1);
        QCOMPARE(dns.servers.at(0).address, QHostAddress(QStringLiteral("192.168.1.1")));
        QCOMPARE(dns.searchDomains, QStringList({QStringLiteral("lan")}));
        QVERIFY(dns.defaultRoute);
        server.terminate();
        QVERIFY(server.waitForFinished());
    }
};

int main(int argc, char** argv) {
    // ctest launches this executable under a private dbus-run-session.
    if (qgetenv("DBUS_SESSION_BUS_ADDRESS").isEmpty()) {
        std::fputs("Run this test under dbus-run-session.\n", stderr);
        return 2;
    }
    qputenv("DBUS_SYSTEM_BUS_ADDRESS", qgetenv("DBUS_SESSION_BUS_ADDRESS"));
    qDBusRegisterMetaType<QList<QVariantMap>>();
    QCoreApplication app(argc, argv);
    if (app.arguments().contains(QStringLiteral("--service"))) {
        FakeNetworkManager manager;
        auto bus = QDBusConnection::systemBus();
        if (!bus.registerService(service) ||
            !bus.registerVirtualObject(root, &manager, QDBusConnection::SubPath)) {
            return 2;
        }
        std::puts("ready");
        std::fflush(stdout);
        return app.exec();
    }
    TestNetworkManagerWatcher test;
    return QTest::qExec(&test, argc, argv);
}

#include "TestNetworkManagerWatcher.moc"
