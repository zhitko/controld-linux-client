#pragma once

#include <QString>

namespace controld {

class ILinkInfo {
  public:
    virtual ~ILinkInfo() = default;

    virtual int ifindex() const = 0;
    virtual QString interfaceName() const = 0;
};

} // namespace controld
