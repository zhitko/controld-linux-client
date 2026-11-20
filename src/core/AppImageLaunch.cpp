#include "core/AppImageLaunch.hpp"

#include <QStringList>

namespace controld {
namespace {

bool absolutePath(const QString& path) {
    if (!path.startsWith(QLatin1Char('/')) || path.contains(QLatin1Char('\n')) ||
        path.contains(QLatin1Char('\r'))) {
        return false;
    }
    const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    return !parts.contains(QStringLiteral(".")) && !parts.contains(QStringLiteral(".."));
}

} // namespace

AppImageLaunch appImageLaunch(const QProcessEnvironment& environment) {
    AppImageLaunch launch;
    launch.appImagePath = environment.value(QStringLiteral("APPIMAGE"));
    launch.appDir = environment.value(QStringLiteral("APPDIR"));
    launch.active = absolutePath(launch.appImagePath) && absolutePath(launch.appDir);
    return launch;
}

QString autostartExecutable(const QProcessEnvironment& environment,
                            const QString& bundledExecutable) {
    const AppImageLaunch launch = appImageLaunch(environment);
    if (launch.active) {
        return launch.appImagePath;
    }
    return bundledExecutable;
}

} // namespace controld
