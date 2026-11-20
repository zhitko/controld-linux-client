#pragma once

#include "core/Types.hpp"

#include <QByteArray>
#include <QDBusArgument>
#include <QList>
#include <QString>

namespace controld {

struct DnsServerExWire {
    int family = 0;
    QByteArray address;
    quint16 port = 0;
    QString sni;

    bool operator==(const DnsServerExWire& other) const {
        return family == other.family && address == other.address && port == other.port &&
               sni == other.sni;
    }
};

struct SearchDomainWire {
    QString name;
    bool routingOnly = false;

    bool operator==(const SearchDomainWire& other) const {
        return name == other.name && routingOnly == other.routingOnly;
    }
};

QDBusArgument& operator<<(QDBusArgument& argument, const DnsServerExWire& server);
const QDBusArgument& operator>>(const QDBusArgument& argument, DnsServerExWire& server);
QDBusArgument& operator<<(QDBusArgument& argument, const SearchDomainWire& domain);
const QDBusArgument& operator>>(const QDBusArgument& argument, SearchDomainWire& domain);

void registerResolvedMetaTypes();

QByteArray ipAddressToBytes(const QHostAddress& address, int* family, QString* error);
QHostAddress ipAddressFromBytes(int family, const QByteArray& bytes, QString* error);
QList<DnsServerExWire> serversToWire(const QList<DnsServerEndpoint>& servers, QString* error);
QList<SearchDomainWire> domainsToWire(const QList<SearchDomain>& domains);
QList<QHostAddress> decodeResolveAddresses(const QDBusArgument& argument, QString* error);

} // namespace controld

Q_DECLARE_METATYPE(controld::DnsServerExWire)
Q_DECLARE_METATYPE(controld::SearchDomainWire)
