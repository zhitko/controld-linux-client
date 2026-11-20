#pragma once

#include "core/Types.hpp"

#include <QString>

namespace controld {

class IChromePolicyWriter {
  public:
    virtual ~IChromePolicyWriter() = default;

    virtual CallResult apply(const QString& resolverId) = 0;
    virtual CallResult clear() = 0;
};

} // namespace controld
