#pragma once

#include <QString>

namespace controld {

QString resolverIdError(const QString& resolverId);
bool isValidResolverId(const QString& resolverId);
QString normalizedProfileName(const QString& name);
QString profileNameError(const QString& name);

} // namespace controld
