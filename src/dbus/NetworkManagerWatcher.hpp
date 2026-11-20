#pragma once

#include "core/ILinkInfo.hpp"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

namespace controld {

class NetworkManagerWatcher : public QObject, public ILinkInfo {
    Q_OBJECT

  public:
    explicit NetworkManagerWatcher(QObject* parent = nullptr);

    int ifindex() const override;
    QString interfaceName() const override;
    LinkDnsConfiguration linkDns(int ifindex) const override;
    QString lastError() const;
    void start();

  signals:
    void primaryLinkChanged();
    void primaryLinkActivated();

  private slots:
    void onPropertiesChanged(const QString& interface, const QVariantMap& changed,
                             const QStringList& invalidated);
    void onLegacyPropertiesChanged(const QVariantMap& changed);
    void onActiveStateChanged(uint state, uint reason);
    void onNameOwnerChanged(const QString& name, const QString& oldOwner, const QString& newOwner);
    void refresh();

  private:
    void schedule();
    void subscribePrimary(const QString& path);
    bool readLink(int* ifindex, QString* name, QString* primaryPath, QString* error) const;

    int m_ifindex = 0;
    QString m_name;
    QString m_error;
    QString m_primaryPath;
    QString m_subscribedPath;
    bool m_notify = false;
    bool m_activationPending = false;
    QTimer m_timer;
};

} // namespace controld
