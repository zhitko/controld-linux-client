#pragma once

#include <QString>

namespace controld {

class AutostartManager {
  public:
    static QString filePath();
    static QString iconInstallPath();
    static bool isEnabled();
    static bool installIcon(const QString& pngPath, QString* error);
    static bool setEnabled(bool enabled, const QString& executable, QString* error,
                           const QString& iconName = QStringLiteral("controld-tray"));
};

} // namespace controld
