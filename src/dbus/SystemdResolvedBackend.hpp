#pragma once

#include "core/IResolvedBackend.hpp"

namespace controld {

class SystemdResolvedBackend : public IResolvedBackend {
  public:
    SystemdResolvedBackend();

    CallResult setLinkDnsEx(int ifindex, const QList<DnsServerEndpoint>& servers) override;
    CallResult setLinkDnsOverTls(int ifindex, const QString& mode) override;
    CallResult setLinkDomains(int ifindex, const QList<SearchDomain>& domains) override;
    CallResult setLinkDefaultRoute(int ifindex, bool enable) override;
    CallResult revertLink(int ifindex) override;
    CallResult flushCaches() override;
    ResolveResult resolveHostname(const QString& name, quint64 flags) override;
};

} // namespace controld
