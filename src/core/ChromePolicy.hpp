#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace controld {

class ChromePolicy {
  public:
    static QStringList managedPolicyPaths();
    static QByteArray document(const QString& resolverId, QString* error);
};

} // namespace controld
