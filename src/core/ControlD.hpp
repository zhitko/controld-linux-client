#pragma once

#include "core/Types.hpp"

#include <QString>
#include <QStringList>

namespace controld {

class ControlD {
  public:
    static const QStringList& anycastAddresses();
    static QString dotHostname(const QString& resolverId);
    static QString dohTemplate(const QString& resolverId);
    static QList<DnsServerEndpoint> serversFor(const QString& resolverId, QString* error);
};

} // namespace controld
