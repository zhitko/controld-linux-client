#pragma once

#include <QHostAddress>
#include <QList>
#include <QString>

#include <cstdint>

namespace controld {

struct CallResult {
    bool ok = true;
    QString errorName;
    QString errorMessage;

    static CallResult success();
    static CallResult failure(QString name, QString message);
    QString summary() const;
};

struct DnsServerEndpoint {
    QHostAddress address;
    quint16 port = 0;
    QString sni;

    bool operator==(const DnsServerEndpoint& other) const {
        return address == other.address && port == other.port && sni == other.sni;
    }
};

struct SearchDomain {
    QString name;
    bool routingOnly = false;

    SearchDomain() = default;
    SearchDomain(QString domainName, bool routing)
        : name(std::move(domainName)), routingOnly(routing) {}

    bool operator==(const SearchDomain& other) const {
        return name == other.name && routingOnly == other.routingOnly;
    }
};

struct ResolveResult {
    bool ok = false;
    QString canonicalName;
    QList<QHostAddress> addresses;
    quint64 flags = 0;
    QString errorName;
    QString errorMessage;

    QString errorSummary() const;
};

enum class RuntimeStatus { Disabled, Active, Failed };

struct OperationResult {
    bool ok = false;
    RuntimeStatus status = RuntimeStatus::Disabled;
    QString detail;
    QString notification;
    bool suggestChromeRestart = false;
};

struct Profile {
    QString id;
    QString name;
    QString resolverId;

    bool operator==(const Profile& other) const {
        return id == other.id && name == other.name && resolverId == other.resolverId;
    }
};

inline QString noSuchLinkErrorName() {
    return QStringLiteral("org.freedesktop.resolve1.NoSuchLink");
}

} // namespace controld
