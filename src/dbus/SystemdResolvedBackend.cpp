#include "dbus/SystemdResolvedBackend.hpp"

#include "dbus/ResolvedMarshalling.hpp"

#include <QDBusConnection>
#include <QDBusMessage>

#include <functional>

#include <netinet/in.h>

namespace controld {
namespace {

constexpr int kCallTimeoutMs = 120000;

QDBusMessage invoke(const QString& method, const std::function<void(QDBusMessage&)>& fill) {
    QDBusMessage message = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.resolve1"), QStringLiteral("/org/freedesktop/resolve1"),
        QStringLiteral("org.freedesktop.resolve1.Manager"), method);
    message.setInteractiveAuthorizationAllowed(true);
    fill(message);
    return QDBusConnection::systemBus().call(message, QDBus::BlockWithGui, kCallTimeoutMs);
}

CallResult fromReply(const QDBusMessage& reply) {
    if (reply.type() == QDBusMessage::ReplyMessage) {
        return CallResult::success();
    }
    QString message = reply.errorMessage();
    if (message.isEmpty() && reply.errorName().isEmpty()) {
        message = QStringLiteral("systemd-resolved did not answer.");
    }
    return CallResult::failure(reply.errorName(), message);
}

} // namespace

SystemdResolvedBackend::SystemdResolvedBackend(QString policyPath)
    : m_policyPath(std::move(policyPath)) {
    registerResolvedMetaTypes();
}

CallResult SystemdResolvedBackend::setLinkDnsEx(int ifindex,
                                                const QList<DnsServerEndpoint>& servers) {
    if (servers.isEmpty()) {
        return CallResult::failure({}, QStringLiteral("No DNS servers to set."));
    }
    QString error;
    const QList<DnsServerExWire> wire = serversToWire(servers, &error);
    if (!error.isEmpty()) {
        return CallResult::failure({}, error);
    }
    const CallResult authorized = authorizeDnsChanges(m_policyPath);
    if (!authorized.ok) {
        return authorized;
    }
    return fromReply(invoke(QStringLiteral("SetLinkDNSEx"), [&](QDBusMessage& message) {
        message << ifindex << QVariant::fromValue(wire);
    }));
}

CallResult SystemdResolvedBackend::setLinkDnsOverTls(int ifindex, const QString& mode) {
    const CallResult authorized = authorizeDnsChanges(m_policyPath);
    if (!authorized.ok) {
        return authorized;
    }
    return fromReply(invoke(QStringLiteral("SetLinkDNSOverTLS"),
                            [&](QDBusMessage& message) { message << ifindex << mode; }));
}

CallResult SystemdResolvedBackend::setLinkDomains(int ifindex, const QList<SearchDomain>& domains) {
    const CallResult authorized = authorizeDnsChanges(m_policyPath);
    if (!authorized.ok) {
        return authorized;
    }
    const QList<SearchDomainWire> wire = domainsToWire(domains);
    return fromReply(invoke(QStringLiteral("SetLinkDomains"), [&](QDBusMessage& message) {
        message << ifindex << QVariant::fromValue(wire);
    }));
}

CallResult SystemdResolvedBackend::setLinkDefaultRoute(int ifindex, bool enable) {
    const CallResult authorized = authorizeDnsChanges(m_policyPath);
    if (!authorized.ok) {
        return authorized;
    }
    return fromReply(invoke(QStringLiteral("SetLinkDefaultRoute"),
                            [&](QDBusMessage& message) { message << ifindex << enable; }));
}

CallResult SystemdResolvedBackend::revertLink(int ifindex) {
    const CallResult authorized = authorizeDnsChanges(m_policyPath);
    if (!authorized.ok) {
        return authorized;
    }
    return fromReply(
        invoke(QStringLiteral("RevertLink"), [&](QDBusMessage& message) { message << ifindex; }));
}

CallResult SystemdResolvedBackend::flushCaches() {
    return fromReply(invoke(QStringLiteral("FlushCaches"), [](QDBusMessage&) {}));
}

ResolveResult SystemdResolvedBackend::resolveHostname(const QString& name, quint64 flags) {
    const QDBusMessage reply =
        invoke(QStringLiteral("ResolveHostname"), [&](QDBusMessage& message) {
            message << int(0) << name << int(AF_UNSPEC) << flags;
        });
    ResolveResult result;
    if (reply.type() != QDBusMessage::ReplyMessage) {
        result.errorName = reply.errorName();
        result.errorMessage = reply.errorMessage().isEmpty()
                                  ? QStringLiteral("systemd-resolved did not answer.")
                                  : reply.errorMessage();
        return result;
    }
    if (reply.arguments().size() < 3) {
        result.errorMessage = QStringLiteral("ResolveHostname returned an unexpected reply.");
        return result;
    }
    const auto addresses = reply.arguments().at(0).value<QDBusArgument>();
    QString decodeError;
    result.addresses = decodeResolveAddresses(addresses, &decodeError);
    if (!decodeError.isEmpty()) {
        result.errorMessage = decodeError;
        return result;
    }
    result.canonicalName = reply.arguments().at(1).toString();
    result.flags = reply.arguments().at(2).toULongLong();
    result.ok = true;
    return result;
}

} // namespace controld
