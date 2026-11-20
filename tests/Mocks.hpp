#pragma once

#include "core/IChromePolicyWriter.hpp"
#include "core/ILinkInfo.hpp"
#include "core/IResolvedBackend.hpp"

#include <QHash>
#include <QList>
#include <QStringList>

namespace controld {

struct RecordedCall {
    QString method;
    int ifindex = 0;
    QString text;
    QList<DnsServerEndpoint> servers;
    QList<SearchDomain> domains;
    bool flag = false;
    quint64 flags = 0;
};

class MockResolved final : public IResolvedBackend {
  public:
    QList<RecordedCall> calls;
    QHash<QString, CallResult> failures;
    int failRevertIfindex = -1;
    CallResult revertFailure = CallResult::success();
    ResolveResult nextResolve;

    MockResolved() {
        nextResolve.ok = true;
        nextResolve.canonicalName = QStringLiteral("api.controld.com");
        nextResolve.addresses.append(QHostAddress(QStringLiteral("147.185.34.1")));
    }

    CallResult setLinkDnsEx(int ifindex, const QList<DnsServerEndpoint>& servers) override {
        RecordedCall call;
        call.method = QStringLiteral("setLinkDnsEx");
        call.ifindex = ifindex;
        call.servers = servers;
        calls.append(call);
        return take(QStringLiteral("setLinkDnsEx"));
    }

    CallResult setLinkDnsOverTls(int ifindex, const QString& mode) override {
        RecordedCall call;
        call.method = QStringLiteral("setLinkDnsOverTls");
        call.ifindex = ifindex;
        call.text = mode;
        calls.append(call);
        return take(QStringLiteral("setLinkDnsOverTls"));
    }

    CallResult setLinkDomains(int ifindex, const QList<SearchDomain>& domains) override {
        RecordedCall call;
        call.method = QStringLiteral("setLinkDomains");
        call.ifindex = ifindex;
        call.domains = domains;
        calls.append(call);
        return take(QStringLiteral("setLinkDomains"));
    }

    CallResult setLinkDefaultRoute(int ifindex, bool enable) override {
        RecordedCall call;
        call.method = QStringLiteral("setLinkDefaultRoute");
        call.ifindex = ifindex;
        call.flag = enable;
        calls.append(call);
        return take(QStringLiteral("setLinkDefaultRoute"));
    }

    CallResult revertLink(int ifindex) override {
        RecordedCall call;
        call.method = QStringLiteral("revertLink");
        call.ifindex = ifindex;
        calls.append(call);
        if (ifindex == failRevertIfindex) {
            return revertFailure;
        }
        return take(QStringLiteral("revertLink"));
    }

    CallResult flushCaches() override {
        RecordedCall call;
        call.method = QStringLiteral("flushCaches");
        calls.append(call);
        return take(QStringLiteral("flushCaches"));
    }

    ResolveResult resolveHostname(const QString& name, quint64 flags) override {
        RecordedCall call;
        call.method = QStringLiteral("resolveHostname");
        call.text = name;
        call.flags = flags;
        calls.append(call);
        return nextResolve;
    }

  private:
    CallResult take(const QString& method) {
        if (!failures.contains(method)) {
            return CallResult::success();
        }
        return failures.take(method);
    }
};

class MockLink final : public ILinkInfo {
  public:
    int index = 3;
    QString name = QStringLiteral("wlan0");
    LinkDnsConfiguration dns;

    MockLink() {
        DnsServerEndpoint endpoint;
        endpoint.address = QHostAddress(QStringLiteral("192.168.1.1"));
        dns.servers.append(endpoint);
        dns.defaultRoute = true;
    }

    int ifindex() const override {
        return index;
    }

    QString interfaceName() const override {
        return name;
    }

    LinkDnsConfiguration linkDns(int ifindex) const override {
        Q_UNUSED(ifindex);
        return dns;
    }
};

class MockChrome final : public IChromePolicyWriter {
  public:
    QStringList applied;
    int clears = 0;
    CallResult applyResult = CallResult::success();
    CallResult clearResult = CallResult::success();

    CallResult apply(const QString& resolverId) override {
        applied.append(resolverId);
        return applyResult;
    }

    CallResult clear() override {
        ++clears;
        return clearResult;
    }
};

} // namespace controld
