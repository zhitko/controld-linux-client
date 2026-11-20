#include "dbus/ResolvedMarshalling.hpp"

#include <QDBusMetaType>

#include <netinet/in.h>

namespace controld {

QByteArray ipAddressToBytes(const QHostAddress& address, int* family, QString* error) {
    if (address.protocol() == QAbstractSocket::IPv4Protocol) {
        const quint32 host = address.toIPv4Address();
        QByteArray bytes(4, 0);
        bytes[0] = static_cast<char>((host >> 24) & 0xFF);
        bytes[1] = static_cast<char>((host >> 16) & 0xFF);
        bytes[2] = static_cast<char>((host >> 8) & 0xFF);
        bytes[3] = static_cast<char>(host & 0xFF);
        if (family != nullptr) {
            *family = AF_INET;
        }
        if (error != nullptr) {
            error->clear();
        }
        return bytes;
    }
    if (address.protocol() == QAbstractSocket::IPv6Protocol) {
        const Q_IPV6ADDR ipv6 = address.toIPv6Address();
        if (family != nullptr) {
            *family = AF_INET6;
        }
        if (error != nullptr) {
            error->clear();
        }
        return QByteArray(reinterpret_cast<const char*>(ipv6.c), 16);
    }
    if (family != nullptr) {
        *family = 0;
    }
    if (error != nullptr) {
        *error = QStringLiteral("DNS server address is not an IPv4 or IPv6 address.");
    }
    return {};
}

QHostAddress ipAddressFromBytes(int family, const QByteArray& bytes, QString* error) {
    if (family == AF_INET) {
        if (bytes.size() != 4) {
            if (error != nullptr) {
                *error = QStringLiteral("IPv4 address had an unexpected length.");
            }
            return {};
        }
        const auto* raw = reinterpret_cast<const unsigned char*>(bytes.constData());
        const quint32 host = (quint32(raw[0]) << 24) | (quint32(raw[1]) << 16) |
                             (quint32(raw[2]) << 8) | quint32(raw[3]);
        if (error != nullptr) {
            error->clear();
        }
        return QHostAddress(host);
    }
    if (family == AF_INET6) {
        if (bytes.size() != 16) {
            if (error != nullptr) {
                *error = QStringLiteral("IPv6 address had an unexpected length.");
            }
            return {};
        }
        QHostAddress address;
        address.setAddress(reinterpret_cast<const quint8*>(bytes.constData()));
        if (error != nullptr) {
            error->clear();
        }
        return address;
    }
    if (error != nullptr) {
        *error = QStringLiteral("Resolved address family is not IPv4 or IPv6.");
    }
    return {};
}

QDBusArgument& operator<<(QDBusArgument& argument, const DnsServerExWire& server) {
    argument.beginStructure();
    argument << server.family << server.address << server.port << server.sni;
    argument.endStructure();
    return argument;
}

const QDBusArgument& operator>>(const QDBusArgument& argument, DnsServerExWire& server) {
    argument.beginStructure();
    argument >> server.family >> server.address >> server.port >> server.sni;
    argument.endStructure();
    return argument;
}

QDBusArgument& operator<<(QDBusArgument& argument, const SearchDomainWire& domain) {
    argument.beginStructure();
    argument << domain.name << domain.routingOnly;
    argument.endStructure();
    return argument;
}

const QDBusArgument& operator>>(const QDBusArgument& argument, SearchDomainWire& domain) {
    argument.beginStructure();
    argument >> domain.name >> domain.routingOnly;
    argument.endStructure();
    return argument;
}

void registerResolvedMetaTypes() {
    static const bool registered = []() {
        qRegisterMetaType<DnsServerExWire>();
        qRegisterMetaType<SearchDomainWire>();
        qDBusRegisterMetaType<DnsServerExWire>();
        qDBusRegisterMetaType<SearchDomainWire>();
        qDBusRegisterMetaType<QList<DnsServerExWire>>();
        qDBusRegisterMetaType<QList<SearchDomainWire>>();
        return true;
    }();
    Q_UNUSED(registered);
}

QList<DnsServerExWire> serversToWire(const QList<DnsServerEndpoint>& servers, QString* error) {
    QList<DnsServerExWire> wire;
    for (const DnsServerEndpoint& server : servers) {
        DnsServerExWire encoded;
        encoded.port = server.port;
        encoded.sni = server.sni;
        QString addressError;
        encoded.address = ipAddressToBytes(server.address, &encoded.family, &addressError);
        if (!addressError.isEmpty()) {
            if (error != nullptr) {
                *error = addressError;
            }
            return {};
        }
        wire.append(encoded);
    }
    if (error != nullptr) {
        error->clear();
    }
    return wire;
}

QList<SearchDomainWire> domainsToWire(const QList<SearchDomain>& domains) {
    QList<SearchDomainWire> wire;
    for (const SearchDomain& domain : domains) {
        SearchDomainWire encoded;
        encoded.name = domain.name;
        encoded.routingOnly = domain.routingOnly;
        wire.append(encoded);
    }
    return wire;
}

QList<QHostAddress> decodeResolveAddresses(const QDBusArgument& argument, QString* error) {
    QList<QHostAddress> addresses;
    argument.beginArray();
    while (!argument.atEnd()) {
        int interfaceIndex = 0;
        int family = 0;
        QByteArray bytes;
        argument.beginStructure();
        argument >> interfaceIndex >> family >> bytes;
        argument.endStructure();
        Q_UNUSED(interfaceIndex);
        QString addressError;
        const QHostAddress address = ipAddressFromBytes(family, bytes, &addressError);
        if (!addressError.isEmpty()) {
            if (error != nullptr) {
                *error = addressError;
            }
            argument.endArray();
            return {};
        }
        addresses.append(address);
    }
    argument.endArray();
    if (error != nullptr) {
        error->clear();
    }
    return addresses;
}

} // namespace controld
