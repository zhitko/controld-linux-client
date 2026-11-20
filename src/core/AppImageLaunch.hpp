#pragma once

#include <QProcessEnvironment>
#include <QString>

namespace controld {

struct AppImageLaunch {
    bool active = false;
    QString appImagePath;
    QString appDir;
};

AppImageLaunch appImageLaunch(const QProcessEnvironment& environment);
QString autostartExecutable(const QProcessEnvironment& environment,
                            const QString& bundledExecutable);

} // namespace controld
