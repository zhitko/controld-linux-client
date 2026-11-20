#pragma once

#include "core/IChromePolicyWriter.hpp"
#include "core/ILinkInfo.hpp"
#include "core/IResolvedBackend.hpp"
#include "core/ProfileStore.hpp"

#include <QObject>
#include <QString>

namespace controld {

class ProfileController : public QObject {
    Q_OBJECT

  public:
    ProfileController(ProfileStore& store, IResolvedBackend& resolved, ILinkInfo& links,
                      IChromePolicyWriter& chrome, QObject* parent = nullptr);

    const ProfileStore& store() const;
    RuntimeStatus status() const;
    QString statusLine() const;
    QString statusDetail() const;

    QString addProfile(const QString& name, const QString& resolverId, QString* error);
    OperationResult updateProfile(const QString& id, const QString& name,
                                  const QString& resolverId);
    OperationResult removeProfile(const QString& id);
    OperationResult activateProfile(const QString& id);
    OperationResult disable();
    OperationResult flush();
    OperationResult setChromePolicyEnabled(bool enabled);
    OperationResult startup();

  public slots:
    void onPrimaryLinkChanged();
    void onPrimaryLinkActivated();

  signals:
    void statusChanged();
    void profilesChanged();

  private:
    enum class QueuedOperation { None, Apply, Disable };

    template <typename Function>
    OperationResult runExclusive(Function&& function, bool announce, bool disableRequest);
    OperationResult applyDesired(bool announce);
    OperationResult applyDesiredNow(bool announce);
    OperationResult disableNow(bool announce);
    OperationResult finish(bool ok, RuntimeStatus status, const QString& detail, bool announce,
                           bool mentionChromeCache, bool policyChanged);
    QString activeInterfaceLabel() const;

    ProfileStore& m_store;
    IResolvedBackend& m_resolved;
    ILinkInfo& m_links;
    IChromePolicyWriter& m_chrome;
    RuntimeStatus m_status = RuntimeStatus::Disabled;
    QString m_detail;
    bool m_applying = false;
    QueuedOperation m_queued = QueuedOperation::None;
};

} // namespace controld
