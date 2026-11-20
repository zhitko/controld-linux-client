#pragma once

#include "core/IResolvedBackend.hpp"
#include "dbus/DnsAuthorization.hpp"

namespace controld {

class SystemdResolvedBackend : public IResolvedBackend {
  public:
    explicit SystemdResolvedBackend(QString policyPath = defaultDnsPolicyPath());

    CallResult setLinkDnsEx(int ifindex, const QList<DnsServerEndpoint>& servers) override;
    CallResult setLinkDnsOverTls(int ifindex, const QString& mode) override;
    CallResult setLinkDomains(int ifindex, const QList<SearchDomain>& domains) override;
    CallResult setLinkDefaultRoute(int ifindex, bool enable) override;
    CallResult revertLink(int ifindex) override;
    CallResult flushCaches() override;
    ResolveResult resolveHostname(const QString& name, quint64 flags) override;

  private:
    QString m_policyPath;
};

} // namespace controld
