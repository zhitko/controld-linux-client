#include "core/ControlD.hpp"
#include "core/HelperInstall.hpp"
#include "dbus/DnsAuthorization.hpp"
#include "dbus/SystemdResolvedBackend.hpp"

#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusVirtualObject>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>
#include <QXmlStreamReader>

#include <cstdio>

namespace {
const QString service = QStringLiteral("org.freedesktop.PolicyKit1");
const QString path = QStringLiteral("/org/freedesktop/PolicyKit1/Authority");
using Details = QMap<QString, QString>;

class Authority final : public QDBusVirtualObject {
  public:
    bool allowed = true;
    int checks = 0;
    int mutations = 0;

    QString introspect(const QString& objectPath) const override {
        Q_UNUSED(objectPath);
        return {};
    }

    bool handleMessage(const QDBusMessage& request, const QDBusConnection& bus) override {
        if (request.member() == QLatin1String("CheckAuthorization")) {
            ++checks;
            if (request.signature() != QLatin1String("(sa{sv})sa{ss}us")) {
                bus.send(request.createErrorReply(QStringLiteral("org.example.InvalidSignature"),
                                                  request.signature()));
                return true;
            }
            const auto args = request.arguments();
            const auto subject = args.at(0).value<QDBusArgument>();
            QString kind;
            QVariantMap subjectDetails;
            subject.beginStructure();
            subject >> kind >> subjectDetails;
            subject.endStructure();
            const Details details = qdbus_cast<Details>(args.at(2));
            if (kind != QLatin1String("system-bus-name") ||
                subjectDetails.value(QStringLiteral("name")).toString() != request.service() ||
                args.at(1).toString() != controld::dnsPolicyActionId() || !details.isEmpty() ||
                args.at(3).toUInt() != 1 || !args.at(4).toString().isEmpty()) {
                bus.send(request.createErrorReply(QStringLiteral("org.example.InvalidSubject"),
                                                  QStringLiteral("Invalid authorization request")));
                return true;
            }
            QDBusArgument result;
            result.beginStructure();
            result << allowed << !allowed << Details {};
            result.endStructure();
            bus.send(request.createReply(QVariant::fromValue(result)));
            return true;
        }
        if (request.member() == QLatin1String("SetAllowed")) {
            allowed = request.arguments().first().toBool();
        } else if (request.member() == QLatin1String("Stats")) {
            bus.send(request.createReply(QVariantList {checks, mutations}));
            return true;
        } else if (request.interface() == QLatin1String("org.freedesktop.resolve1.Manager")) {
            ++mutations;
        } else {
            return false;
        }
        bus.send(request.createReply());
        return true;
    }
};

QDBusMessage command(const QString& name, const QVariantList& args = {}) {
    auto message =
        QDBusMessage::createMethodCall(service, path, QStringLiteral("org.example.Test"), name);
    message.setArguments(args);
    return QDBusConnection::systemBus().call(message);
}
} // namespace

class TestDnsAuthorization : public QObject {
    Q_OBJECT
  private slots:
    void checksCallerAndRespectsDenialAndRevocation() {
        QProcess server;
        server.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--service")});
        QVERIFY(server.waitForStarted());
        QVERIFY(server.waitForReadyRead());
        QCOMPARE(server.readAllStandardOutput().trimmed(), QByteArray("ready"));
        QTemporaryDir dir;
        const QString policyPath = dir.filePath(QStringLiteral("policy"));
        QVERIFY(QFile::copy(QStringLiteral(CONTROLD_TEST_POLICY), policyPath));
        QFile policy(policyPath);
        QVERIFY(policy.open(QIODevice::ReadOnly));
        QVERIFY(controld::policyHasDnsAuthorization(QString::fromUtf8(policy.readAll())));
        controld::SystemdResolvedBackend backend(policyPath);
        QVERIFY(
            backend.setLinkDnsEx(3, controld::ControlD::serversFor(QStringLiteral("aaaa"), nullptr))
                .ok);
        QVERIFY(backend.setLinkDnsOverTls(3, QStringLiteral("yes")).ok);
        QVERIFY(backend.setLinkDomains(3, {{QStringLiteral("."), true}}).ok);
        QVERIFY(backend.setLinkDefaultRoute(3, true).ok);
        QVERIFY(backend.revertLink(3).ok);
        auto stats = command(QStringLiteral("Stats"));
        QCOMPARE(stats.arguments(), QVariantList({5, 5}));
        QCOMPARE(command(QStringLiteral("SetAllowed"), {false}).type(), QDBusMessage::ReplyMessage);
        QVERIFY(!backend.setLinkDefaultRoute(3, true).ok);
        stats = command(QStringLiteral("Stats"));
        QCOMPARE(stats.arguments(), QVariantList({6, 5}));
        // No cached local allow decision after the authority revokes access.
        QVERIFY(!backend.revertLink(3).ok);
        stats = command(QStringLiteral("Stats"));
        QCOMPARE(stats.arguments(), QVariantList({7, 5}));
        // Without our installed action, resolved remains responsible for authorization.
        controld::SystemdResolvedBackend fallback(dir.filePath(QStringLiteral("absent")));
        QVERIFY(fallback.revertLink(3).ok);
        stats = command(QStringLiteral("Stats"));
        QCOMPARE(stats.arguments(), QVariantList({7, 6}));
        server.terminate();
        QVERIFY(server.waitForFinished());
    }

    void ignoresOlderOrMalformedPolicy() {
        QVERIFY(!controld::policyHasDnsAuthorization(
            QStringLiteral("<policyconfig><action id=\"other\"/></policyconfig>")));
        QVERIFY(!controld::policyHasDnsAuthorization(QStringLiteral(
            "<policyconfig><action id=\"com.vzhitko.controldtray.configure-dns\">")));
    }

    void installedPolicyGroupsOnlyDnsMutations() {
        QFile policy(QStringLiteral(CONTROLD_TEST_POLICY));
        QVERIFY(policy.open(QIODevice::ReadOnly));
        QXmlStreamReader xml(policy.readAll());
        QString currentAction;
        QStringList implied;
        QString activeDefault;
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isEndElement() && xml.name() == QLatin1String("action")) {
                currentAction.clear();
            }
            if (!xml.isStartElement()) {
                continue;
            }
            if (xml.name() == QLatin1String("action")) {
                currentAction = xml.attributes().value(QStringLiteral("id")).toString();
            } else if (currentAction == controld::dnsPolicyActionId()) {
                if (xml.name() == QLatin1String("annotate") &&
                    xml.attributes().value(QStringLiteral("key")) ==
                        QLatin1String("org.freedesktop.policykit.imply")) {
                    implied = xml.readElementText().split(QLatin1Char(' '), Qt::SkipEmptyParts);
                } else if (xml.name() == QLatin1String("allow_active")) {
                    activeDefault = xml.readElementText();
                }
            }
        }
        QVERIFY(!xml.hasError());
        QCOMPARE(activeDefault, QStringLiteral("auth_admin_keep"));
        QCOMPARE(implied, QStringList({QStringLiteral("org.freedesktop.resolve1.set-dns-servers"),
                                       QStringLiteral("org.freedesktop.resolve1.set-dns-over-tls"),
                                       QStringLiteral("org.freedesktop.resolve1.set-domains"),
                                       QStringLiteral("org.freedesktop.resolve1.set-default-route"),
                                       QStringLiteral("org.freedesktop.resolve1.revert")}));
    }
};

int main(int argc, char** argv) {
    if (qgetenv("DBUS_SESSION_BUS_ADDRESS").isEmpty()) {
        return 2;
    }
    qputenv("DBUS_SYSTEM_BUS_ADDRESS", qgetenv("DBUS_SESSION_BUS_ADDRESS"));
    QCoreApplication app(argc, argv);
    qDBusRegisterMetaType<Details>();
    if (app.arguments().contains(QStringLiteral("--service"))) {
        Authority authority;
        auto bus = QDBusConnection::systemBus();
        if (!bus.registerService(service) ||
            !bus.registerService(QStringLiteral("org.freedesktop.resolve1")) ||
            !bus.registerVirtualObject(QStringLiteral("/"), &authority, QDBusConnection::SubPath)) {
            return 2;
        }
        std::puts("ready");
        std::fflush(stdout);
        return app.exec();
    }
    TestDnsAuthorization test;
    return QTest::qExec(&test, argc, argv);
}

#include "TestDnsAuthorization.moc"
