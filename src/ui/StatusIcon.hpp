#pragma once

#include "core/Types.hpp"

#include <QIcon>

namespace controld {

class StatusIcon {
  public:
    static QIcon create(RuntimeStatus status);
};

} // namespace controld
