#include "dbus/PkexecChromePolicyWriter.hpp"

#include "core/AppImageLaunch.hpp"
#include "core/HelperInstall.hpp"
#include "core/Validation.hpp"

#include <QCoreApplication>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>

namespace controld {
namespace {

constexpr int kTimeoutMs = 120000;

QString pkexecFailure(int code, const QString& helperError) {
    if (!helperError.trimmed().isEmpty()) {
        return helperError.trimmed();
    }
    if (code == 126) {
        return QStringLiteral("The polkit prompt was dismissed.");
    }
    if (code == 127) {
        return QStringLiteral("pkexec could not obtain authorization or find the helper.");
    }
    return QStringLiteral("The Chrome policy helper exited with status %1.").arg(code);
}

} // namespace

QString defaultChromeHelperPath() {
    if (appImageLaunch(QProcessEnvironment::systemEnvironment()).active) {
#ifdef CONTROLD_TRAY_HELPER_PATH
        return QStringLiteral(CONTROLD_TRAY_HELPER_PATH);
#else
        return helperDestination();
#endif
    }
    const QString beside =
        QCoreApplication::applicationDirPath() + QStringLiteral("/controld-tray-chrome-policy");
    if (QFileInfo::exists(beside)) {
        return beside;
    }
    const QString relative = QFileInfo(QCoreApplication::applicationDirPath() +
                                       QStringLiteral("/../libexec/controld-tray-chrome-policy"))
                                 .absoluteFilePath();
    if (QFileInfo::exists(relative)) {
        return relative;
    }
#ifdef CONTROLD_TRAY_HELPER_PATH
    return QStringLiteral(CONTROLD_TRAY_HELPER_PATH);
#else
    return QStringLiteral("/usr/libexec/controld-tray-chrome-policy");
#endif
}

PkexecChromePolicyWriter::PkexecChromePolicyWriter(QString helperPath)
    : m_helperPath(std::move(helperPath)) {}

CallResult PkexecChromePolicyWriter::apply(const QString& resolverId) {
    const QString idError = resolverIdError(resolverId);
    if (!idError.isEmpty()) {
        return CallResult::failure({}, idError);
    }
    const CallResult ready = validateHelper();
    if (!ready.ok) {
        return ready;
    }
    return run({QStringLiteral("apply"), resolverId});
}

CallResult PkexecChromePolicyWriter::clear() {
    const CallResult ready = validateHelper();
    if (!ready.ok) {
        return ready;
    }
    return run({QStringLiteral("clear")});
}

CallResult PkexecChromePolicyWriter::validateHelper() const {
    const QFileInfo helper(m_helperPath);
    if (helper.isSymLink() || !helper.exists() || !helper.isFile()) {
        return CallResult::failure(
            {},
            QStringLiteral("The Chrome policy helper is not installed at %1. DNS profile switching "
                           "does not need it. From the AppImage, use Install helper. A Debian "
                           "package install puts the helper there for you.")
                .arg(m_helperPath));
    }
    const QFileDevice::Permissions permissions = helper.permissions();
    if (helper.ownerId() != 0 ||
        (permissions & (QFileDevice::WriteGroup | QFileDevice::WriteOther)) != 0) {
        return CallResult::failure(
            {},
            QStringLiteral("pkexec will refuse %1 until it is owned by root and not writable "
                           "by group or other. Install the package to use Chrome policy pinning.")
                .arg(m_helperPath));
    }
    if (QStandardPaths::findExecutable(QStringLiteral("pkexec")).isEmpty()) {
        return CallResult::failure({}, QStringLiteral("pkexec was not found on PATH."));
    }
    return CallResult::success();
}

CallResult PkexecChromePolicyWriter::run(const QStringList& arguments) const {
    const QString pkexec = QStandardPaths::findExecutable(QStringLiteral("pkexec"));
    QProcess process;
    process.setProgram(pkexec);
    QStringList fullArguments;
    fullArguments.append(m_helperPath);
    fullArguments.append(arguments);
    process.setArguments(fullArguments);
    process.start();
    if (!process.waitForStarted(kTimeoutMs)) {
        return CallResult::failure(
            {}, QStringLiteral("Could not start pkexec: %1").arg(process.errorString()));
    }
    if (!process.waitForFinished(kTimeoutMs)) {
        process.kill();
        process.waitForFinished(5000);
        return CallResult::failure({}, QStringLiteral("pkexec did not finish."));
    }
    const QString helperError = QString::fromUtf8(process.readAllStandardError()).trimmed();
    if (process.exitStatus() != QProcess::NormalExit) {
        return CallResult::failure({}, pkexecFailure(-1, helperError));
    }
    if (process.exitCode() != 0) {
        return CallResult::failure({}, pkexecFailure(process.exitCode(), helperError));
    }
    return CallResult::success();
}

} // namespace controld
