#pragma once

#include "core/Types.hpp"

#include <QString>
#include <QStringList>

namespace controld {

struct LinkDnsConfiguration {
    QList<DnsServerEndpoint> servers;
    QStringList searchDomains;
    bool defaultRoute = false;
    QString error;
};

class ILinkInfo {
  public:
    virtual ~ILinkInfo() = default;

    virtual int ifindex() const = 0;
    virtual QString interfaceName() const = 0;
    virtual LinkDnsConfiguration linkDns(int ifindex) const = 0;
};

} // namespace controld
