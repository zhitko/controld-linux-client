#pragma once

#include "core/Types.hpp"

#include <QString>

namespace controld {

class IResolvedBackend {
  public:
    virtual ~IResolvedBackend() = default;

    virtual CallResult setLinkDnsEx(int ifindex, const QList<DnsServerEndpoint>& servers) = 0;
    virtual CallResult setLinkDnsOverTls(int ifindex, const QString& mode) = 0;
    virtual CallResult setLinkDomains(int ifindex, const QList<SearchDomain>& domains) = 0;
    virtual CallResult setLinkDefaultRoute(int ifindex, bool enable) = 0;
    virtual CallResult revertLink(int ifindex) = 0;
    virtual CallResult flushCaches() = 0;
    virtual ResolveResult resolveHostname(const QString& name, quint64 flags) = 0;
};

} // namespace controld
