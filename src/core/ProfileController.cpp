#include "core/ProfileController.hpp"

#include "core/ControlD.hpp"
#include "core/Validation.hpp"
#include "core/Verification.hpp"

namespace controld {
namespace {

QString chromeCacheHint(bool policyChanged) {
    QString text =
        QStringLiteral("\n\nChrome keeps its own hostname cache. This app cannot flush it. "
                       "Restart Chrome if lookups still use the previous result.");
    if (policyChanged) {
        text += QStringLiteral(
            " The managed Secure DNS policy changed and is read when Chrome starts.");
    }
    return text;
}

} // namespace

ProfileController::ProfileController(ProfileStore& store, IResolvedBackend& resolved,
                                     ILinkInfo& links, IChromePolicyWriter& chrome, QObject* parent)
    : QObject(parent), m_store(store), m_resolved(resolved), m_links(links), m_chrome(chrome),
      m_detail(QStringLiteral("ControlD is off. DNS settings were not changed.")) {}

const ProfileStore& ProfileController::store() const {
    return m_store;
}

RuntimeStatus ProfileController::status() const {
    return m_status;
}

QString ProfileController::statusLine() const {
    const std::optional<Profile> selected = m_store.profile(m_store.activeProfileId());
    const QString name = selected.has_value() ? selected->name : QString();
    switch (m_status) {
    case RuntimeStatus::Active:
        return QStringLiteral("Active: %1").arg(name);
    case RuntimeStatus::Failed:
        return name.isEmpty() ? QStringLiteral("Failed") : QStringLiteral("Failed: %1").arg(name);
    case RuntimeStatus::Disabled:
        return QStringLiteral("Off");
    }
    return QStringLiteral("Off");
}

QString ProfileController::statusDetail() const {
    return m_detail;
}

QString ProfileController::addProfile(const QString& name, const QString& resolverId,
                                      QString* error) {
    const QString id = m_store.addProfile(name, resolverId, error);
    if (!id.isEmpty()) {
        emit profilesChanged();
    }
    return id;
}

OperationResult ProfileController::updateProfile(const QString& id, const QString& name,
                                                 const QString& resolverId) {
    const bool wasActive = m_store.activeProfileId() == id;
    QString error;
    if (!m_store.updateProfile(id, name, resolverId, &error)) {
        OperationResult result;
        result.ok = false;
        result.status = m_status;
        result.detail = m_detail;
        result.notification = error;
        return result;
    }
    emit profilesChanged();
    if (!wasActive) {
        OperationResult result;
        result.ok = true;
        result.status = m_status;
        result.detail = m_detail;
        return result;
    }
    return applyDesired(true);
}

OperationResult ProfileController::removeProfile(const QString& id) {
    const bool wasActive = m_store.activeProfileId() == id;
    QString error;
    if (!m_store.removeProfile(id, &error)) {
        OperationResult result;
        result.ok = false;
        result.status = m_status;
        result.detail = m_detail;
        result.notification = error;
        return result;
    }
    emit profilesChanged();
    if (!wasActive) {
        OperationResult result;
        result.ok = true;
        result.status = m_status;
        result.detail = m_detail;
        return result;
    }
    return disable();
}

OperationResult ProfileController::activateProfile(const QString& id) {
    if (!m_store.profile(id).has_value()) {
        OperationResult result;
        result.ok = false;
        result.status = m_status;
        result.detail = m_detail;
        result.notification = QStringLiteral("Profile was not found.");
        return result;
    }
    QString error;
    if (!m_store.setActiveProfileId(id, &error)) {
        return persistenceFailure(error);
    }
    return applyDesired(true);
}

OperationResult ProfileController::disable() {
    return runExclusive([this](bool announce) { return disableNow(announce); }, true, true);
}

OperationResult ProfileController::flush() {
    const CallResult flushed = m_resolved.flushCaches();
    if (!flushed.ok) {
        OperationResult result;
        result.ok = false;
        result.status = m_status;
        result.detail = m_detail;
        result.notification =
            QStringLiteral("Could not flush the DNS cache: %1").arg(flushed.summary());
        return result;
    }

    if (m_store.activeProfileId().isEmpty()) {
        OperationResult result;
        result.ok = true;
        result.status = m_status;
        result.detail = m_detail;
        result.suggestChromeRestart = true;
        result.notification = QStringLiteral("DNS cache flushed.") + chromeCacheHint(false);
        return result;
    }

    const std::optional<Profile> selected = m_store.profile(m_store.activeProfileId());
    if (!selected.has_value() || m_appliedProfileId != selected->id ||
        m_appliedResolverId != selected->resolverId) {
        return finish(false, RuntimeStatus::Failed,
                      QStringLiteral("DNS cache flushed, but the selected profile has not been "
                                     "successfully applied. Activate it again to retry."),
                      true, true, false);
    }
    const ResolveResult lookup = m_resolved.resolveHostname(
        QString::fromLatin1(Verification::queryName()), Verification::queryFlags());
    if (!Verification::passed(lookup)) {
        return finish(false, RuntimeStatus::Failed, Verification::failureDetail(lookup), true, true,
                      false);
    }

    const QString detail =
        QStringLiteral("Profile \"%1\" (%2) is active on %3. Verified %4 canonical name %5.")
            .arg(selected->name, selected->resolverId, activeInterfaceLabel(),
                 QString::fromLatin1(Verification::queryName()),
                 QString::fromLatin1(Verification::expectedCanonical()));
    return finish(true, RuntimeStatus::Active, detail, true, true, false);
}

OperationResult ProfileController::setChromePolicyEnabled(bool enabled) {
    OperationResult result;
    result.status = m_status;
    result.detail = m_detail;

    if (!enabled) {
        if (m_store.chromePolicyDirty() || !m_store.chromePinnedResolverId().isEmpty()) {
            const CallResult cleared = clearChromePolicy();
            if (!cleared.ok) {
                result.ok = false;
                result.notification =
                    QStringLiteral("Could not remove the Chrome policy: %1").arg(cleared.summary());
                return result;
            }
            result.suggestChromeRestart = true;
        }
        QString error;
        if (!m_store.setChromePolicyEnabled(false, &error)) {
            return persistenceFailure(error);
        }
        result.ok = true;
        if (result.notification.isEmpty()) {
            result.notification =
                result.suggestChromeRestart
                    ? QStringLiteral(
                          "Chrome Secure DNS pinning is off. The managed policy was removed.") +
                          chromeCacheHint(true)
                    : QStringLiteral("Chrome Secure DNS pinning is off.");
        }
        return result;
    }

    QString error;
    if (!m_store.setChromePolicyEnabled(true, &error)) {
        return persistenceFailure(error);
    }
    const std::optional<Profile> selected = m_store.profile(m_store.activeProfileId());
    if (!selected.has_value()) {
        result.ok = true;
        result.notification = QStringLiteral("Chrome Secure DNS pinning is on. The policy file is "
                                             "written when a profile is activated.");
        return result;
    }
    if (!m_store.chromePolicyDirty() && m_store.chromePinnedResolverId() == selected->resolverId) {
        result.ok = true;
        result.notification =
            QStringLiteral("Chrome Secure DNS is already pinned to this profile.");
        return result;
    }
    const CallResult written = applyChromePolicy(selected->resolverId);
    if (!written.ok) {
        result.ok = false;
        result.notification =
            QStringLiteral("Could not write the Chrome policy: %1").arg(written.summary());
        return result;
    }
    result.ok = true;
    result.suggestChromeRestart = true;
    result.notification =
        QStringLiteral("Chrome Secure DNS is pinned to %1.").arg(selected->resolverId) +
        chromeCacheHint(true);
    return result;
}

OperationResult ProfileController::startup() {
    if (!m_store.activeProfileId().isEmpty()) {
        return applyDesired(false);
    }
    if (!m_store.appliedIfindexes().isEmpty() || m_store.chromePolicyDirty() ||
        !m_store.chromePinnedResolverId().isEmpty()) {
        return runExclusive([this](bool announce) { return disableNow(announce); }, false, true);
    }
    return finish(true, RuntimeStatus::Disabled,
                  QStringLiteral("ControlD is off. DNS settings were not changed."), false, false,
                  false);
}

void ProfileController::onPrimaryLinkChanged() {
    if (m_store.activeProfileId().isEmpty()) {
        if (!m_store.appliedIfindexes().isEmpty()) {
            runExclusive([this](bool announce) { return disableNow(announce); }, false, true);
        }
        return;
    }
    const int current = m_links.ifindex();
    const QList<int> applied = m_store.appliedIfindexes();
    if (m_status == RuntimeStatus::Active && current > 0 && applied.size() == 1 &&
        applied.first() == current) {
        return;
    }
    applyDesired(false);
}

void ProfileController::onPrimaryLinkActivated() {
    if (m_store.activeProfileId().isEmpty()) {
        if (!m_store.appliedIfindexes().isEmpty()) {
            runExclusive([this](bool announce) { return disableNow(announce); }, false, true);
        }
        return;
    }
    applyDesired(false);
}

template <typename Function>
OperationResult ProfileController::runExclusive(Function&& function, bool announce,
                                                bool disableRequest) {
    if (m_applying) {
        if (disableRequest || m_queued == QueuedOperation::Disable) {
            m_queued = QueuedOperation::Disable;
        } else {
            m_queued = QueuedOperation::Apply;
        }
        OperationResult result;
        result.ok = true;
        result.status = m_status;
        result.detail = m_detail;
        return result;
    }

    m_applying = true;
    OperationResult result = function(announce);
    m_applying = false;
    while (m_queued != QueuedOperation::None) {
        const QueuedOperation operation = m_queued;
        m_queued = QueuedOperation::None;
        m_applying = true;
        if (operation == QueuedOperation::Disable || m_store.activeProfileId().isEmpty()) {
            result = disableNow(false);
        } else {
            result = applyDesiredNow(false);
        }
        m_applying = false;
    }
    return result;
}

OperationResult ProfileController::applyDesired(bool announce) {
    return runExclusive([this](bool interactive) { return applyDesiredNow(interactive); }, announce,
                        false);
}

OperationResult ProfileController::applyDesiredNow(bool announce) {
    m_appliedProfileId.clear();
    m_appliedResolverId.clear();
    const QString id = m_store.activeProfileId();
    if (id.isEmpty()) {
        return disableNow(announce);
    }
    const std::optional<Profile> selected = m_store.profile(id);
    if (!selected.has_value()) {
        QString error;
        if (!m_store.setActiveProfileId({}, &error)) {
            return persistenceFailure(error, announce);
        }
        return finish(false, RuntimeStatus::Failed,
                      QStringLiteral("The selected profile no longer exists."), announce, false,
                      false);
    }
    if (!isValidResolverId(selected->resolverId)) {
        return finish(false, RuntimeStatus::Failed, resolverIdError(selected->resolverId), announce,
                      false, false);
    }

    const int ifindex = m_links.ifindex();
    if (ifindex <= 0) {
        return finish(false, RuntimeStatus::Failed,
                      QStringLiteral("No active NetworkManager connection."), announce, false,
                      false);
    }

    QString serverError;
    const QList<DnsServerEndpoint> servers =
        ControlD::serversFor(selected->resolverId, &serverError);
    if (servers.isEmpty()) {
        return finish(false, RuntimeStatus::Failed,
                      serverError.isEmpty() ? QStringLiteral("No ControlD servers were produced.")
                                            : serverError,
                      announce, false, false);
    }

    // Persist cleanup intent before the first system mutation.
    const QList<int> previousDirty = m_store.appliedIfindexes();
    QList<int> dirty = m_store.appliedIfindexes();
    if (!dirty.contains(ifindex)) {
        dirty.append(ifindex);
    }
    QString persistenceError;
    if (!m_store.setAppliedIfindexes(dirty, &persistenceError)) {
        return persistenceFailure(persistenceError, announce);
    }
    const CallResult dns = m_resolved.setLinkDnsEx(ifindex, servers);
    if (!dns.ok) {
        QString detail = QStringLiteral("Could not set DNS servers: %1").arg(dns.summary());
        if (!m_store.setAppliedIfindexes(previousDirty, &persistenceError)) {
            detail += QLatin1Char(' ') + persistenceError;
        }
        return finish(false, RuntimeStatus::Failed, detail, announce, false, false);
    }

    const CallResult dot = m_resolved.setLinkDnsOverTls(ifindex, QStringLiteral("yes"));
    if (!dot.ok) {
        return finish(false, RuntimeStatus::Failed,
                      QStringLiteral("Could not enable DNS-over-TLS: %1").arg(dot.summary()),
                      announce, false, false);
    }

    const CallResult domains =
        m_resolved.setLinkDomains(ifindex, {SearchDomain(QStringLiteral("."), true)});
    if (!domains.ok) {
        return finish(
            false, RuntimeStatus::Failed,
            QStringLiteral("Could not set the '~.' routing domain: %1").arg(domains.summary()),
            announce, false, false);
    }

    const CallResult route = m_resolved.setLinkDefaultRoute(ifindex, true);
    if (!route.ok) {
        return finish(
            false, RuntimeStatus::Failed,
            QStringLiteral("Could not set the DNS default route: %1").arg(route.summary()),
            announce, false, false);
    }

    QStringList warnings;
    QList<int> remaining;
    remaining.append(ifindex);
    for (const int old : dirty) {
        if (old == ifindex) {
            continue;
        }
        const CallResult reverted = m_resolved.revertLink(old);
        if (reverted.errorName == noSuchLinkErrorName()) {
            continue;
        }
        if (!reverted.ok) {
            remaining.append(old);
            warnings.append(QStringLiteral("Could not revert interface %1: %2")
                                .arg(old)
                                .arg(reverted.summary()));
            continue;
        }
        bool restored = false;
        const QString restoreError = restoreLinkDns(old, &restored);
        if (!restoreError.isEmpty()) {
            remaining.append(old);
            warnings.append(restoreError);
        }
    }
    if (!m_store.setAppliedIfindexes(remaining, &persistenceError)) {
        return persistenceFailure(persistenceError, announce);
    }

    const CallResult flushed = m_resolved.flushCaches();
    if (!flushed.ok) {
        return finish(false, RuntimeStatus::Failed,
                      QStringLiteral("Could not flush the DNS cache: %1").arg(flushed.summary()),
                      announce, false, false);
    }

    const ResolveResult lookup = m_resolved.resolveHostname(
        QString::fromLatin1(Verification::queryName()), Verification::queryFlags());
    if (!Verification::passed(lookup)) {
        return finish(false, RuntimeStatus::Failed, Verification::failureDetail(lookup), announce,
                      false, false);
    }

    m_appliedProfileId = selected->id;
    m_appliedResolverId = selected->resolverId;

    QString detail =
        QStringLiteral("Profile \"%1\" (%2) is active on %3. Verified %4 canonical name %5.")
            .arg(selected->name, selected->resolverId, activeInterfaceLabel(),
                 QString::fromLatin1(Verification::queryName()),
                 QString::fromLatin1(Verification::expectedCanonical()));

    bool policyChanged = false;
    bool policyFailed = false;
    if (m_store.chromePolicyEnabled() &&
        (m_store.chromePolicyDirty() || m_store.chromePinnedResolverId() != selected->resolverId)) {
        const CallResult written = applyChromePolicy(selected->resolverId);
        if (!written.ok) {
            policyFailed = true;
            detail += QStringLiteral(" Chrome policy was not updated: %1").arg(written.summary());
        } else {
            policyChanged = true;
        }
    }
    if (!warnings.isEmpty()) {
        detail += QLatin1Char(' ') + warnings.join(QLatin1Char(' '));
    }

    return finish(!policyFailed && warnings.isEmpty(), RuntimeStatus::Active, detail, announce,
                  true, policyChanged);
}

OperationResult ProfileController::disableNow(bool announce) {
    QString error;
    if (!m_store.setActiveProfileId({}, &error)) {
        return persistenceFailure(error, announce);
    }
    m_appliedProfileId.clear();
    m_appliedResolverId.clear();
    const QList<int> dirty = m_store.appliedIfindexes();
    const bool chromeNeedsClear =
        m_store.chromePolicyDirty() || !m_store.chromePinnedResolverId().isEmpty();
    if (dirty.isEmpty() && !chromeNeedsClear) {
        return finish(true, RuntimeStatus::Disabled,
                      QStringLiteral("ControlD is off. DNS settings were not changed."), announce,
                      false, false);
    }

    QStringList problems;
    QList<int> stillDirty;
    QStringList restoredLabels;
    bool missingServers = false;
    for (const int ifindex : dirty) {
        const CallResult reverted = m_resolved.revertLink(ifindex);
        if (!reverted.ok && reverted.errorName != noSuchLinkErrorName()) {
            return finish(false, RuntimeStatus::Failed,
                          QStringLiteral("Could not restore default DNS on interface %1: %2")
                              .arg(ifindex)
                              .arg(reverted.summary()),
                          announce, false, false);
        }
        if (!reverted.ok) {
            continue;
        }
        bool restored = false;
        const QString restoreError = restoreLinkDns(ifindex, &restored);
        if (!restoreError.isEmpty()) {
            problems.append(restoreError);
            stillDirty.append(ifindex);
            continue;
        }
        if (restored) {
            restoredLabels.append(m_links.ifindex() == ifindex && !m_links.interfaceName().isEmpty()
                                      ? m_links.interfaceName()
                                      : QString::number(ifindex));
        } else {
            missingServers = true;
        }
    }

    const bool revertedSomething = !dirty.isEmpty();
    if (!m_store.setAppliedIfindexes(stillDirty, &error)) {
        return persistenceFailure(error, announce);
    }

    const CallResult flushed = m_resolved.flushCaches();
    if (!flushed.ok) {
        problems.append(QStringLiteral("Could not flush the DNS cache: %1").arg(flushed.summary()));
    }

    bool policyChanged = false;
    if (chromeNeedsClear) {
        const CallResult cleared = clearChromePolicy();
        if (!cleared.ok) {
            problems.append(
                QStringLiteral("Could not remove the Chrome policy: %1").arg(cleared.summary()));
        } else {
            policyChanged = true;
        }
    }

    if (!problems.isEmpty()) {
        return finish(false, RuntimeStatus::Failed,
                      QStringLiteral("DNS settings were reverted, but %1")
                          .arg(problems.join(QLatin1Char(' '))),
                      announce, policyChanged, policyChanged);
    }

    QStringList sentences;
    sentences.append(QStringLiteral("ControlD is off."));
    if (!restoredLabels.isEmpty()) {
        sentences.append(QStringLiteral("NetworkManager's DNS servers were written back on %1.")
                             .arg(restoredLabels.join(QStringLiteral(", "))));
    }
    if (revertedSomething && missingServers) {
        sentences.append(
            QStringLiteral("Per-link DNS settings made by this app were reverted. NetworkManager "
                           "has no DNS servers to write back for a link, so that link stays "
                           "without a resolver until its connection is activated again."));
    }
    if (policyChanged) {
        sentences.append(QStringLiteral("The managed Chrome Secure DNS policy was removed."));
    }
    return finish(true, RuntimeStatus::Disabled, sentences.join(QLatin1Char(' ')), announce, true,
                  policyChanged);
}

OperationResult ProfileController::finish(bool ok, RuntimeStatus status, const QString& detail,
                                          bool announce, bool mentionChromeCache,
                                          bool policyChanged) {
    m_status = status;
    m_detail = detail;
    emit statusChanged();

    OperationResult result;
    result.ok = ok;
    result.status = status;
    result.detail = detail;
    result.suggestChromeRestart = policyChanged || (announce && mentionChromeCache);
    if (announce || !ok || policyChanged) {
        result.notification = detail;
    }
    if ((announce || policyChanged) && mentionChromeCache) {
        result.notification += chromeCacheHint(policyChanged);
    }
    return result;
}

QString ProfileController::restoreLinkDns(int ifindex, bool* restored) {
    if (restored != nullptr) {
        *restored = false;
    }
    const LinkDnsConfiguration config = m_links.linkDns(ifindex);
    if (!config.error.isEmpty()) {
        return config.error;
    }
    if (config.servers.isEmpty()) {
        return {};
    }

    const CallResult dns = m_resolved.setLinkDnsEx(ifindex, config.servers);
    if (!dns.ok) {
        return QStringLiteral("Could not write NetworkManager DNS on interface %1: %2")
            .arg(ifindex)
            .arg(dns.summary());
    }
    if (!config.searchDomains.isEmpty()) {
        QList<SearchDomain> domains;
        for (const QString& name : config.searchDomains) {
            domains.append(SearchDomain(name, false));
        }
        const CallResult written = m_resolved.setLinkDomains(ifindex, domains);
        if (!written.ok) {
            return QStringLiteral("Could not write search domains on interface %1: %2")
                .arg(ifindex)
                .arg(written.summary());
        }
    }
    if (config.defaultRoute) {
        const CallResult route = m_resolved.setLinkDefaultRoute(ifindex, true);
        if (!route.ok) {
            return QStringLiteral("Could not restore the DNS default route on interface %1: %2")
                .arg(ifindex)
                .arg(route.summary());
        }
    }
    if (restored != nullptr) {
        *restored = true;
    }
    return {};
}

QString ProfileController::activeInterfaceLabel() const {
    if (!m_links.interfaceName().isEmpty()) {
        return m_links.interfaceName();
    }
    if (m_links.ifindex() > 0) {
        return QString::number(m_links.ifindex());
    }
    return QStringLiteral("the active link");
}

OperationResult ProfileController::persistenceFailure(const QString& error, bool announce) {
    return finish(false, RuntimeStatus::Failed, error, announce, false, false);
}

CallResult ProfileController::applyChromePolicy(const QString& resolverId) {
    QString error;
    if (!m_store.setChromePolicyDirty(true, &error)) {
        return CallResult::failure({}, error);
    }
    const CallResult written = m_chrome.apply(resolverId);
    if (!written.ok) {
        return written;
    }
    if (!m_store.setChromePinnedResolverId(resolverId, &error) ||
        !m_store.setChromePolicyDirty(false, &error)) {
        return CallResult::failure({}, error);
    }
    return CallResult::success();
}

CallResult ProfileController::clearChromePolicy() {
    QString error;
    if (!m_store.setChromePolicyDirty(true, &error)) {
        return CallResult::failure({}, error);
    }
    const CallResult cleared = m_chrome.clear();
    if (!cleared.ok) {
        return cleared;
    }
    if (!m_store.setChromePinnedResolverId({}, &error) ||
        !m_store.setChromePolicyDirty(false, &error)) {
        return CallResult::failure({}, error);
    }
    return CallResult::success();
}

} // namespace controld
