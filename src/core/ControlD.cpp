#include "core/ControlD.hpp"

#include "core/Validation.hpp"

namespace controld {

const QStringList& ControlD::anycastAddresses() {
    // Primary bootstrap addresses for dns.controld.com.
    // https://docs.controld.com/docs/control-d-ip-ranges
    static const QStringList addresses = {
        QStringLiteral("76.76.2.22"),
        QStringLiteral("76.76.10.22"),
        QStringLiteral("2606:1a40::22"),
        QStringLiteral("2606:1a40:1::22"),
    };
    return addresses;
}

QString ControlD::dotHostname(const QString& resolverId) {
    return resolverId + QStringLiteral(".dns.controld.com");
}

QString ControlD::dohTemplate(const QString& resolverId) {
    return QStringLiteral("https://dns.controld.com/") + resolverId;
}

QList<DnsServerEndpoint> ControlD::serversFor(const QString& resolverId, QString* error) {
    const QString idError = resolverIdError(resolverId);
    if (!idError.isEmpty()) {
        if (error != nullptr) {
            *error = idError;
        }
        return {};
    }

    const QString hostname = dotHostname(resolverId);
    QList<DnsServerEndpoint> servers;
    for (const QString& addressText : anycastAddresses()) {
        const QHostAddress address(addressText);
        if (address.isNull()) {
            if (error != nullptr) {
                *error = QStringLiteral("ControlD address could not be parsed: ") + addressText;
            }
            return {};
        }
        DnsServerEndpoint endpoint;
        endpoint.address = address;
        endpoint.port = 0;
        endpoint.sni = hostname;
        servers.append(endpoint);
    }
    if (error != nullptr) {
        error->clear();
    }
    return servers;
}

} // namespace controld
