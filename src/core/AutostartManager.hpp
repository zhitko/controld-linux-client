#pragma once

#include <QString>

namespace controld {

class AutostartManager {
  public:
    static QString filePath();
    static bool isEnabled();
    static bool setEnabled(bool enabled, const QString& executable, QString* error);
};

} // namespace controld
