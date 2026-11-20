#pragma once

#include "core/Types.hpp"

namespace controld {

QString defaultDnsPolicyPath();
CallResult authorizeDnsChanges(const QString& policyPath);

} // namespace controld
