#include "dbus/DnsAuthorization.hpp"

#include "core/HelperInstall.hpp"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QFile>

namespace controld {

QString defaultDnsPolicyPath() {
#ifdef CONTROLD_TRAY_POLICY_PATH
    return QStringLiteral(CONTROLD_TRAY_POLICY_PATH);
#else
    return policyDestination();
#endif
}

CallResult authorizeDnsChanges(const QString& policyPath) {
    QFile policy(policyPath);
    // Uninstalled builds and older AppImages retain resolved's own prompts.
    if (!policy.open(QIODevice::ReadOnly) || policy.size() > 256 * 1024 ||
        !policyHasDnsAuthorization(QString::fromUtf8(policy.readAll()))) {
        return CallResult::success();
    }

    const auto bus = QDBusConnection::systemBus();
    if (!bus.isConnected()) {
        return CallResult::failure({}, QStringLiteral("System D-Bus is not available."));
    }
    using Details = QMap<QString, QString>;
    qDBusRegisterMetaType<Details>();
    QDBusArgument subject;
    subject.beginStructure();
    subject << QStringLiteral("system-bus-name")
            << QVariantMap {{QStringLiteral("name"), bus.baseService()}};
    subject.endStructure();

    auto request =
        QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.PolicyKit1"),
                                       QStringLiteral("/org/freedesktop/PolicyKit1/Authority"),
                                       QStringLiteral("org.freedesktop.PolicyKit1.Authority"),
                                       QStringLiteral("CheckAuthorization"));
    request.setInteractiveAuthorizationAllowed(true);
    // Empty details are required for a client checking its own authorization.
    request << QVariant::fromValue(subject) << dnsPolicyActionId()
            << QVariant::fromValue(Details {}) << uint(1) << QString();
    const auto reply = bus.call(request, QDBus::BlockWithGui, 120000);
    if (reply.type() != QDBusMessage::ReplyMessage) {
        return CallResult::failure(reply.errorName(), reply.errorMessage());
    }
    if (reply.arguments().size() != 1 ||
        reply.arguments().first().userType() != qMetaTypeId<QDBusArgument>()) {
        return CallResult::failure({}, QStringLiteral("Unexpected DNS authorization reply."));
    }
    const auto result = reply.arguments().first().value<QDBusArgument>();
    if (result.currentSignature() != QLatin1String("(bba{ss})")) {
        return CallResult::failure({}, QStringLiteral("Unexpected DNS authorization reply."));
    }
    bool authorized = false;
    bool challenge = false;
    Details details;
    result.beginStructure();
    result >> authorized >> challenge >> details;
    result.endStructure();
    if (!authorized) {
        return CallResult::failure(QStringLiteral("org.freedesktop.PolicyKit1.Error.NotAuthorized"),
                                   QStringLiteral("DNS authorization was denied or canceled."));
    }
    // Do not cache this decision ourselves: polkit controls expiry and revocation.
    return CallResult::success();
}

} // namespace controld
