#include "core/AutostartManager.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

namespace controld {
namespace {

const QString kMarker = QStringLiteral("X-ControlD-Tray-Autostart=true");

QString quoteExec(const QString& executable) {
    static const QRegularExpression safe(QStringLiteral("^[A-Za-z0-9_./@+-]+$"));
    if (safe.match(executable).hasMatch()) {
        return executable;
    }
    QString escaped = executable;
    escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

QString readFile(const QString& path, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = QStringLiteral("Could not read %1.").arg(path);
        }
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

} // namespace

QString AutostartManager::filePath() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    return base + QStringLiteral("/autostart/controld-tray.desktop");
}

bool AutostartManager::isEnabled() {
    const QString path = filePath();
    if (!QFile::exists(path)) {
        return false;
    }
    const QString text = readFile(path, nullptr);
    return text.contains(kMarker) && !text.contains(QStringLiteral("Hidden=true"));
}

bool AutostartManager::setEnabled(bool enabled, const QString& executable, QString* error) {
    const QString path = filePath();
    if (!enabled) {
        if (!QFile::exists(path)) {
            if (error != nullptr) {
                error->clear();
            }
            return true;
        }
        const QString text = readFile(path, error);
        if (text.isEmpty() && error != nullptr && !error->isEmpty()) {
            return false;
        }
        if (!text.contains(kMarker)) {
            if (error != nullptr) {
                *error = QStringLiteral("Refusing to remove an autostart file that was not created "
                                        "by ControlD Tray: %1")
                             .arg(path);
            }
            return false;
        }
        if (!QFile::remove(path)) {
            if (error != nullptr) {
                *error = QStringLiteral("Could not remove %1.").arg(path);
            }
            return false;
        }
        if (error != nullptr) {
            error->clear();
        }
        return true;
    }

    if (executable.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("The application path is empty.");
        }
        return false;
    }
    if (QFile::exists(path)) {
        const QString text = readFile(path, error);
        if (!text.contains(kMarker)) {
            if (error != nullptr) {
                *error =
                    QStringLiteral(
                        "An autostart file already exists and was not created by ControlD Tray: %1")
                        .arg(path);
            }
            return false;
        }
    }

    QDir().mkpath(QFileInfo(path).dir().absolutePath());
    const QString content = QStringLiteral("[Desktop Entry]\n"
                                           "Type=Application\n"
                                           "Version=1.0\n"
                                           "Name=ControlD Tray\n"
                                           "Comment=Switch ControlD DNS profiles\n"
                                           "Exec=%1\n"
                                           "Icon=controld-tray\n"
                                           "Terminal=false\n"
                                           "Categories=Network;\n"
                                           "X-GNOME-Autostart-enabled=true\n"
                                           "%2\n")
                                .arg(quoteExec(executable), kMarker);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error != nullptr) {
            *error = QStringLiteral("Could not write %1.").arg(path);
        }
        return false;
    }
    if (file.write(content.toUtf8()) != content.toUtf8().size()) {
        file.cancelWriting();
        if (error != nullptr) {
            *error = QStringLiteral("Could not write %1.").arg(path);
        }
        return false;
    }
    if (!file.commit()) {
        if (error != nullptr) {
            *error = QStringLiteral("Could not commit %1.").arg(path);
        }
        return false;
    }
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

} // namespace controld
