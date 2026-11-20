#include "core/HelperInstall.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

#include <cerrno>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

namespace controld {
namespace {

constexpr qint64 kMaxPolicyBytes = 256 * 1024;
constexpr int kPkexecTimeoutMs = 120000;

QString joinRoot(const QString& rootPrefix, const QString& absolutePath) {
    if (rootPrefix.isEmpty()) {
        return absolutePath;
    }
    return rootPrefix + absolutePath;
}

bool fileInsideRoot(const QString& root, const QString& path) {
    if (!root.startsWith(QLatin1Char('/')) || !path.startsWith(QLatin1Char('/'))) {
        return false;
    }
    const QString prefix = root.endsWith(QLatin1Char('/')) ? root : root + QLatin1Char('/');
    if (!path.startsWith(prefix)) {
        return false;
    }
    const QStringList parts = path.mid(prefix.size()).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    return !parts.isEmpty() && !parts.contains(QStringLiteral(".")) &&
           !parts.contains(QStringLiteral(".."));
}

bool sourceNameSafe(const QString& name) {
    if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String("..") ||
        name.contains(QLatin1Char('/'))) {
        return false;
    }
    for (const QChar character : name) {
        if (character.unicode() > 127) {
            return false;
        }
        const bool allowed = character.isLetterOrNumber() || character == QLatin1Char('.') ||
                             character == QLatin1Char('_') || character == QLatin1Char('+') ||
                             character == QLatin1Char('-');
        if (!allowed) {
            return false;
        }
    }
    return true;
}

QString sha256File(const QString& path, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = QStringLiteral("Could not read %1.").arg(path);
        }
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) {
        if (error != nullptr) {
            *error = QStringLiteral("Could not hash %1.").arg(path);
        }
        return {};
    }
    return QString::fromLatin1(hash.result().toHex());
}

bool copyResolvedFile(const QString& source, const QString& destination, QString* error) {
    if (QFile::exists(destination)) {
        if (error != nullptr) {
            *error = QStringLiteral("Staging file already exists: %1").arg(destination);
        }
        return false;
    }
    if (!QFile::copy(source, destination)) {
        if (error != nullptr) {
            *error = QStringLiteral("Could not stage %1.").arg(destination);
        }
        return false;
    }
    return true;
}

QString resolveInsideRoot(const QString& root, const QString& start, QString* error) {
    QString current = start;
    for (int hop = 0; hop < 8; ++hop) {
        if (!fileInsideRoot(root, current)) {
            if (error != nullptr) {
                *error = QStringLiteral("Refusing a path outside the AppImage: %1").arg(current);
            }
            return {};
        }
        struct stat status {};
        if (lstat(current.toUtf8().constData(), &status) != 0) {
            if (error != nullptr) {
                *error = QStringLiteral("Could not inspect %1: %2")
                             .arg(current, QString::fromLocal8Bit(strerror(errno)));
            }
            return {};
        }
        if (S_ISLNK(status.st_mode)) {
            QByteArray target(4096, 0);
            const ssize_t length = readlink(current.toUtf8().constData(), target.data(),
                                            static_cast<size_t>(target.size()));
            if (length < 0 || length >= target.size()) {
                if (error != nullptr) {
                    *error = QStringLiteral("Could not read symlink %1.").arg(current);
                }
                return {};
            }
            target.truncate(static_cast<int>(length));
            const QString raw = QString::fromUtf8(target);
            if (raw.startsWith(QLatin1Char('/'))) {
                current = raw;
            } else {
                current = QFileInfo(current).dir().absolutePath() + QLatin1Char('/') + raw;
            }
            continue;
        }
        if (!S_ISREG(status.st_mode)) {
            if (error != nullptr) {
                *error = QStringLiteral("Not a regular file: %1").arg(current);
            }
            return {};
        }
        return current;
    }
    if (error != nullptr) {
        *error = QStringLiteral("Too many symlinks under %1.").arg(start);
    }
    return {};
}

bool absoluteSafePath(const QString& path) {
    if (!path.startsWith(QLatin1Char('/'))) {
        return false;
    }
    const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    return !parts.isEmpty() && !parts.contains(QStringLiteral(".")) &&
           !parts.contains(QStringLiteral(".."));
}

bool trustedSystemExecutable(const QString& start, QString* error) {
    QString current = start;
    for (int hop = 0; hop < 8; ++hop) {
        if (!absoluteSafePath(current)) {
            if (error != nullptr) {
                *error = QStringLiteral("%1 is not a safe absolute path.").arg(current);
            }
            return false;
        }
        const QStringList parts = current.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        QString walked;
        bool followedLink = false;
        for (int index = 0; index < parts.size(); ++index) {
            walked += QLatin1Char('/') + parts.at(index);
            struct stat status {};
            if (lstat(walked.toUtf8().constData(), &status) != 0) {
                if (error != nullptr) {
                    *error = QStringLiteral("Could not inspect %1.").arg(walked);
                }
                return false;
            }
            if (status.st_uid != 0) {
                if (error != nullptr) {
                    *error = QStringLiteral("%1 is not owned by root.").arg(walked);
                }
                return false;
            }
            if (S_ISLNK(status.st_mode)) {
                QByteArray target(4096, 0);
                const ssize_t length = readlink(walked.toUtf8().constData(), target.data(),
                                                static_cast<size_t>(target.size()));
                if (length < 0 || length >= target.size()) {
                    if (error != nullptr) {
                        *error = QStringLiteral("Could not read symlink %1.").arg(walked);
                    }
                    return false;
                }
                target.truncate(static_cast<int>(length));
                const QString raw = QString::fromUtf8(target);
                current = raw.startsWith(QLatin1Char('/'))
                              ? raw
                              : QFileInfo(walked).dir().absolutePath() + QLatin1Char('/') + raw;
                followedLink = true;
                break;
            }
            const bool last = index + 1 == parts.size();
            const bool groupWritable = (status.st_mode & (S_IWGRP | S_IWOTH)) != 0;
            if (!last) {
                if (!S_ISDIR(status.st_mode) || groupWritable) {
                    if (error != nullptr) {
                        *error = QStringLiteral("%1 is not a root-owned directory.").arg(walked);
                    }
                    return false;
                }
                continue;
            }
            if (!S_ISREG(status.st_mode) || groupWritable) {
                if (error != nullptr) {
                    *error = QStringLiteral("%1 is not a root-owned executable.").arg(walked);
                }
                return false;
            }
            if (error != nullptr) {
                error->clear();
            }
            return true;
        }
        if (followedLink) {
            continue;
        }
        break;
    }
    if (error != nullptr) {
        *error = QStringLiteral("Could not resolve %1.").arg(start);
    }
    return false;
}

bool runPkexec(const QStringList& arguments, QString* error) {
    const QString pkexec = QStandardPaths::findExecutable(QStringLiteral("pkexec"));
    const QString python = QStandardPaths::findExecutable(QStringLiteral("python3"));
    if (pkexec.isEmpty() || python.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("pkexec and python3 are required to install the helper.");
        }
        return false;
    }
    if (!trustedSystemExecutable(pkexec, error) || !trustedSystemExecutable(python, error)) {
        return false;
    }

    QProcess process;
    process.setProgram(pkexec);
    QStringList fullArguments;
    fullArguments.append(python);
    fullArguments.append(arguments);
    process.setArguments(fullArguments);
    process.start();
    if (!process.waitForStarted(kPkexecTimeoutMs)) {
        if (error != nullptr) {
            *error = QStringLiteral("Could not start pkexec: %1").arg(process.errorString());
        }
        return false;
    }
    if (!process.waitForFinished(kPkexecTimeoutMs)) {
        process.kill();
        process.waitForFinished(5000);
        if (error != nullptr) {
            *error = QStringLiteral("pkexec did not finish.");
        }
        return false;
    }
    const QString helperError = QString::fromUtf8(process.readAllStandardError()).trimmed();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (error != nullptr) {
            if (!helperError.isEmpty()) {
                *error = helperError;
            } else if (process.exitCode() == 126) {
                *error = QStringLiteral("The polkit prompt was dismissed.");
            } else if (process.exitCode() == 127) {
                *error = QStringLiteral("pkexec could not obtain authorization or find python3.");
            } else {
                *error = QStringLiteral("The helper installer exited with status %1.")
                             .arg(process.exitCode());
            }
        }
        return false;
    }
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

bool stageFile(const QString& root, const QString& source, const QString& destination,
               QString* error) {
    const QString resolved = resolveInsideRoot(root, source, error);
    if (resolved.isEmpty()) {
        return false;
    }
    return copyResolvedFile(resolved, destination, error);
}

QString readTextFile(const QString& path, qint64 maxBytes, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = QStringLiteral("Could not read %1.").arg(path);
        }
        return {};
    }
    if (file.size() > maxBytes) {
        if (error != nullptr) {
            *error = QStringLiteral("Refusing to read %1 because it is too large.").arg(path);
        }
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

bool helperFileAcceptable(const QFileInfo& info, bool requireRootOwner, QString* detail) {
    if (!info.exists()) {
        if (detail != nullptr) {
            *detail = QStringLiteral("The Chrome policy helper is not installed.");
        }
        return false;
    }
    if (info.isSymLink()) {
        if (detail != nullptr) {
            *detail = QStringLiteral("The Chrome policy helper is a symlink.");
        }
        return false;
    }
    if (!info.isFile()) {
        if (detail != nullptr) {
            *detail = QStringLiteral("The Chrome policy helper is not a regular file.");
        }
        return false;
    }
    const QFileDevice::Permissions permissions = info.permissions();
    if ((permissions & (QFileDevice::WriteGroup | QFileDevice::WriteOther)) != 0 ||
        (permissions & QFileDevice::ExeOwner) == 0) {
        if (detail != nullptr) {
            *detail =
                QStringLiteral("The Chrome policy helper must be executable by its owner and not "
                               "writable by group or other.");
        }
        return false;
    }
    if (requireRootOwner && info.ownerId() != 0) {
        if (detail != nullptr) {
            *detail = QStringLiteral("The Chrome policy helper is not owned by root.");
        }
        return false;
    }
    return true;
}

} // namespace

QString helperDestination() {
    return QStringLiteral("/usr/libexec/controld-tray-chrome-policy");
}

QString policyDestination() {
    return QStringLiteral("/usr/share/polkit-1/actions/com.vzhitko.controldtray.policy");
}

QString libraryDirectory() {
    return QStringLiteral("/usr/lib/controld-tray");
}

QString manifestDestination() {
    return libraryDirectory() + QStringLiteral("/manifest");
}

QString policyActionId() {
    return QStringLiteral("com.vzhitko.controldtray.chrome-policy");
}

bool installDestinationAllowed(const QString& destination) {
    if (destination == helperDestination() || destination == policyDestination()) {
        return true;
    }
    const QString prefix = libraryDirectory() + QLatin1Char('/');
    if (!destination.startsWith(prefix)) {
        return false;
    }
    return sourceNameSafe(destination.mid(prefix.size()));
}

bool policyTextAcceptable(const QString& text) {
    return text.contains(policyActionId()) && text.contains(helperDestination());
}

bool ManifestEntry::operator==(const ManifestEntry& other) const {
    return mode == other.mode && destination == other.destination &&
           sourceName == other.sourceName && sha256 == other.sha256;
}

bool parseManifest(const QString& text, QList<ManifestEntry>* entries, QString* error) {
    if (entries == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("No manifest destination was provided.");
        }
        return false;
    }
    entries->clear();
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (int index = 0; index < lines.size(); ++index) {
        const QString line = lines.at(index).trimmed();
        if (line.isEmpty()) {
            continue;
        }
        const QStringList parts = line.split(QLatin1Char(' '));
        if (parts.size() != 4) {
            if (error != nullptr) {
                *error = QStringLiteral("Invalid manifest line %1.").arg(index + 1);
            }
            return false;
        }
        ManifestEntry entry;
        entry.mode = parts.at(0);
        entry.destination = parts.at(1);
        entry.sourceName = parts.at(2);
        entry.sha256 = parts.at(3);
        const bool modeOk =
            entry.mode == QLatin1String("0644") || entry.mode == QLatin1String("0755");
        bool hashOk = entry.sha256.size() == 64;
        for (const QChar character : entry.sha256) {
            const unsigned short code = character.unicode();
            const bool digit = code >= '0' && code <= '9';
            const bool lower = code >= 'a' && code <= 'f';
            hashOk = hashOk && (digit || lower);
        }
        const QString baseName = entry.destination.section(QLatin1Char('/'), -1);
        if (!modeOk || !installDestinationAllowed(entry.destination) ||
            !sourceNameSafe(entry.sourceName) || entry.sourceName != baseName || !hashOk ||
            entry.destination == manifestDestination()) {
            if (error != nullptr) {
                *error = QStringLiteral("Manifest entry is not allowed: %1").arg(entry.destination);
            }
            return false;
        }
        entries->append(entry);
    }
    bool sawHelper = false;
    bool sawPolicy = false;
    QStringList destinations;
    for (const ManifestEntry& entry : *entries) {
        if (destinations.contains(entry.destination)) {
            if (error != nullptr) {
                *error = QStringLiteral("The install manifest repeats a destination.");
            }
            return false;
        }
        destinations.append(entry.destination);
        sawHelper = sawHelper || entry.destination == helperDestination();
        sawPolicy = sawPolicy || entry.destination == policyDestination();
    }
    if (!sawHelper || !sawPolicy) {
        if (error != nullptr) {
            *error = QStringLiteral("The install manifest is missing the helper or the policy.");
        }
        return false;
    }
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

QString manifestText(const QList<ManifestEntry>& entries) {
    QString text;
    for (const ManifestEntry& entry : entries) {
        text += QStringLiteral("%1 %2 %3 %4\n")
                    .arg(entry.mode, entry.destination, entry.sourceName, entry.sha256);
    }
    return text;
}

QList<LddEntry> parseLddOutput(const QString& text) {
    QList<LddEntry> entries;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (QString line : lines) {
        line = line.trimmed();
        if (line.isEmpty()) {
            continue;
        }
        LddEntry entry;
        const int arrow = line.indexOf(QStringLiteral("=>"));
        if (arrow >= 0) {
            entry.soname = line.left(arrow).trimmed().section(QLatin1Char(' '), 0, 0);
            const QString rhs = line.mid(arrow + 2).trimmed();
            if (rhs.startsWith(QLatin1String("not found"))) {
                entry.found = false;
            } else {
                entry.resolvedPath = rhs.section(QLatin1Char(' '), 0, 0);
                entry.found = entry.resolvedPath.startsWith(QLatin1Char('/'));
            }
        } else {
            const QString token = line.section(QLatin1Char(' '), 0, 0);
            if (token.startsWith(QLatin1Char('/'))) {
                entry.soname = QFileInfo(token).fileName();
                entry.resolvedPath = token;
                entry.found = true;
            } else {
                entry.soname = token;
                entry.found = false;
            }
        }
        if (!entry.soname.isEmpty()) {
            entries.append(entry);
        }
    }
    return entries;
}

bool isHostRuntimeLibrary(const QString& soname) {
    // glibc and libstdc++ come from the host so the AppImage stays compatible with
    // newer distributions. libz and libcom_err are the extra libraries linuxdeploy
    // leaves on the system for this Qt build. Both have been in the Ubuntu base
    // since 20.04. OpenSSL is intentionally absent: libssl.so.1.1 is not on 24.04.
    static const QStringList exact = {
        QStringLiteral("linux-vdso.so.1"), QStringLiteral("libc.so.6"),
        QStringLiteral("libm.so.6"),       QStringLiteral("libdl.so.2"),
        QStringLiteral("libpthread.so.0"), QStringLiteral("librt.so.1"),
        QStringLiteral("libresolv.so.2"),  QStringLiteral("libutil.so.1"),
        QStringLiteral("libgcc_s.so.1"),   QStringLiteral("libstdc++.so.6"),
        QStringLiteral("libz.so.1"),       QStringLiteral("libcom_err.so.2"),
    };
    return exact.contains(soname) || soname.startsWith(QStringLiteral("ld-linux"));
}

LibraryPlan planBundledLibraries(const QList<LddEntry>& entries, const QString& appDir) {
    LibraryPlan plan;
    QStringList seen;
    for (const LddEntry& entry : entries) {
        if (isHostRuntimeLibrary(entry.soname)) {
            continue;
        }
        if (!entry.found || entry.resolvedPath.isEmpty()) {
            plan.error =
                QStringLiteral("The helper needs %1, which was not found inside the AppImage.")
                    .arg(entry.soname);
            return plan;
        }
        if (!fileInsideRoot(appDir, entry.resolvedPath) || !sourceNameSafe(entry.soname)) {
            plan.error = QStringLiteral("The helper needs %1 from outside the AppImage (%2).")
                             .arg(entry.soname, entry.resolvedPath);
            return plan;
        }
        if (seen.contains(entry.soname)) {
            plan.error =
                QStringLiteral("The helper depends on %1 more than once.").arg(entry.soname);
            return plan;
        }
        seen.append(entry.soname);
        BundledLibrary library;
        library.soname = entry.soname;
        library.sourcePath = entry.resolvedPath;
        plan.libraries.append(library);
    }
    plan.ok = true;
    return plan;
}

LibraryPlan librariesForHelper(const QString& helperPath, const QString& appDir) {
    const QString ldd = QStandardPaths::findExecutable(QStringLiteral("ldd"));
    if (ldd.isEmpty()) {
        LibraryPlan plan;
        plan.error =
            QStringLiteral("ldd was not found, so the helper's libraries could not be listed.");
        return plan;
    }
    QProcess process;
    process.setProgram(ldd);
    process.setArguments({helperPath});
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    const QString bundled =
        appDir + QStringLiteral("/usr/lib:") + appDir + QStringLiteral("/usr/lib/x86_64-linux-gnu");
    const QString current = environment.value(QStringLiteral("LD_LIBRARY_PATH"));
    environment.insert(QStringLiteral("LD_LIBRARY_PATH"),
                       current.isEmpty() ? bundled : bundled + QLatin1Char(':') + current);
    process.setProcessEnvironment(environment);
    process.start();
    if (!process.waitForStarted(10000) || !process.waitForFinished(20000)) {
        LibraryPlan plan;
        plan.error = QStringLiteral("ldd did not finish for %1.").arg(helperPath);
        return plan;
    }
    const QString output = QString::fromUtf8(process.readAllStandardOutput());
    const QString errors = QString::fromUtf8(process.readAllStandardError()).trimmed();
    if (output.trimmed().isEmpty()) {
        LibraryPlan plan;
        plan.error = errors.isEmpty()
                         ? QStringLiteral("ldd produced no output for %1.").arg(helperPath)
                         : errors;
        return plan;
    }
    return planBundledLibraries(parseLddOutput(output), appDir);
}

SystemHelperStatus inspectSystemHelper(const QString& rootPrefix, bool requireRootOwner) {
    SystemHelperStatus status;
    const QString helperPath = joinRoot(rootPrefix, helperDestination());
    const QFileInfo helper(helperPath);
    if (!helperFileAcceptable(helper, requireRootOwner, &status.detail)) {
        return status;
    }
    const QString policyPath = joinRoot(rootPrefix, policyDestination());
    const QFileInfo policy(policyPath);
    if (!policy.exists() || policy.isSymLink() || !policy.isFile()) {
        status.detail = QStringLiteral("The polkit policy is not installed.");
        return status;
    }
    QString error;
    const QString text = readTextFile(policyPath, kMaxPolicyBytes, &error);
    if (!policyTextAcceptable(text)) {
        status.detail = error.isEmpty()
                            ? QStringLiteral("The polkit policy is not the ControlD Tray action.")
                            : error;
        return status;
    }

    const QString manifestPath = joinRoot(rootPrefix, manifestDestination());
    const QFileInfo manifest(manifestPath);
    if (manifest.exists() && !manifest.isSymLink() && manifest.isFile()) {
        const QString manifestBody = readTextFile(manifestPath, kMaxPolicyBytes, &error);
        QList<ManifestEntry> entries;
        status.managedByApp =
            !manifestBody.isEmpty() && parseManifest(manifestBody, &entries, &error);
    }
    status.ready = true;
    status.detail = QStringLiteral("The Chrome policy helper is installed.");
    return status;
}

QString appImageHelperNote(const SystemHelperStatus& status) {
    if (status.ready && status.managedByApp) {
        return QStringLiteral(
            "This copy is an AppImage. The Chrome Secure DNS helper is installed in system paths. "
            "Uninstall helper removes those files and does not change DNS settings.");
    }
    if (status.ready) {
        return QStringLiteral("This copy is an AppImage. It will use the Chrome helper already "
                              "installed at %1 "
                              "and will not replace it.")
            .arg(helperDestination());
    }
    if (status.managedByApp) {
        return QStringLiteral("This copy is an AppImage. A previous helper install is incomplete. "
                              "Install helper replaces it, "
                              "or Uninstall helper removes the files that still match.");
    }
    return QStringLiteral("This copy is an AppImage. DNS switching works without installing system "
                          "files. The optional Chrome "
                          "Secure DNS helper is not installed. Install helper copies it into "
                          "system paths after you confirm.");
}

QString helperInstallPrompt(const QStringList& destinations) {
    return QStringLiteral(
               "Install the optional Chrome and Chromium Secure DNS helper?\n\n"
               "Switching ControlD profiles uses systemd-resolved and does not need this helper. "
               "Nothing is written until you confirm. Polkit will then ask to run python3 as the "
               "administrator; that program copies only the files listed below.\n\n"
               "%1\n\n"
               "Uninstall helper removes those files if they still match this install. DNS "
               "settings "
               "are left unchanged.")
        .arg(destinations.join(QLatin1Char('\n')));
}

bool privilegedCopyToolsReady(QString* error) {
    const QString pkexec = QStandardPaths::findExecutable(QStringLiteral("pkexec"));
    const QString python = QStandardPaths::findExecutable(QStringLiteral("python3"));
    if (pkexec.isEmpty() || python.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("pkexec and python3 are required to install the helper.");
        }
        return false;
    }
    return trustedSystemExecutable(pkexec, error) && trustedSystemExecutable(python, error);
}

QString helperUninstallPrompt(const QStringList& destinations) {
    return QStringLiteral(
               "Remove the Chrome and Chromium Secure DNS helper installed for the AppImage?\n\n"
               "This does not change DNS settings. A managed Chrome policy file is removed first "
               "if "
               "this app wrote one. These files are removed only if they still match this "
               "install:\n\n"
               "%1")
        .arg(destinations.join(QLatin1Char('\n')));
}

bool HelperInstallSession::prepare(const AppImageLaunch& launch, QString* error) {
    m_prepared = false;
    m_prompt.clear();
    if (!m_stage.isValid()) {
        if (error != nullptr) {
            *error = QStringLiteral("Could not create a temporary directory.");
        }
        return false;
    }
    if (!launch.active) {
        if (error != nullptr) {
            *error = QStringLiteral("Install helper is only available when running the AppImage.");
        }
        return false;
    }
    const QString helperSource =
        launch.appDir + QStringLiteral("/usr/libexec/controld-tray-chrome-policy");
    const QString policySource =
        launch.appDir +
        QStringLiteral("/usr/share/polkit-1/actions/com.vzhitko.controldtray.policy");
    const QString scriptSource =
        launch.appDir + QStringLiteral("/usr/share/controld-tray/install_system_files.py");
    const QString stagedHelper =
        m_stage.path() + QLatin1Char('/') + QFileInfo(helperDestination()).fileName();
    const QString stagedPolicy =
        m_stage.path() + QLatin1Char('/') + QFileInfo(policyDestination()).fileName();
    const QString stagedScript = m_stage.path() + QStringLiteral("/install_system_files.py");
    if (!stageFile(launch.appDir, helperSource, stagedHelper, error) ||
        !stageFile(launch.appDir, policySource, stagedPolicy, error) ||
        !stageFile(launch.appDir, scriptSource, stagedScript, error)) {
        return false;
    }
    const QString policyBody = readTextFile(stagedPolicy, kMaxPolicyBytes, error);
    if (!policyTextAcceptable(policyBody)) {
        if (error != nullptr) {
            *error =
                QStringLiteral("The AppImage policy file is not the ControlD Tray polkit action.");
        }
        return false;
    }

    const LibraryPlan libraries = librariesForHelper(helperSource, launch.appDir);
    if (!libraries.ok) {
        if (error != nullptr) {
            *error = libraries.error;
        }
        return false;
    }

    QList<ManifestEntry> entries;
    auto appendEntry = [&](const QString& stagedPath, const QString& destination,
                           const QString& mode) -> bool {
        QString hashError;
        ManifestEntry entry;
        entry.mode = mode;
        entry.destination = destination;
        entry.sourceName = QFileInfo(destination).fileName();
        entry.sha256 = sha256File(stagedPath, &hashError);
        if (entry.sha256.isEmpty() || !installDestinationAllowed(entry.destination)) {
            if (error != nullptr) {
                *error = hashError.isEmpty()
                             ? QStringLiteral("Refusing to install %1.").arg(destination)
                             : hashError;
            }
            return false;
        }
        entries.append(entry);
        return true;
    };
    if (!appendEntry(stagedHelper, helperDestination(), QStringLiteral("0755")) ||
        !appendEntry(stagedPolicy, policyDestination(), QStringLiteral("0644"))) {
        return false;
    }
    for (const BundledLibrary& library : libraries.libraries) {
        const QString destination = libraryDirectory() + QLatin1Char('/') + library.soname;
        const QString stagedPath = m_stage.path() + QLatin1Char('/') + library.soname;
        if (!stageFile(launch.appDir, library.sourcePath, stagedPath, error) ||
            !appendEntry(stagedPath, destination, QStringLiteral("0644"))) {
            return false;
        }
    }

    QString manifestError;
    const QString body = manifestText(entries);
    QList<ManifestEntry> parsed;
    if (!parseManifest(body, &parsed, &manifestError)) {
        if (error != nullptr) {
            *error = manifestError;
        }
        return false;
    }
    QFile manifest(m_stage.path() + QStringLiteral("/manifest"));
    if (!manifest.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        manifest.write(body.toUtf8()) != body.toUtf8().size()) {
        if (error != nullptr) {
            *error = QStringLiteral("Could not write the install manifest.");
        }
        return false;
    }
    manifest.close();

    QStringList destinations;
    destinations.append(helperDestination());
    QStringList libraryDestinations;
    for (const BundledLibrary& library : libraries.libraries) {
        libraryDestinations.append(libraryDirectory() + QLatin1Char('/') + library.soname);
    }
    libraryDestinations.sort();
    destinations.append(libraryDestinations);
    destinations.append(policyDestination());
    destinations.append(manifestDestination());
    m_prompt = helperInstallPrompt(destinations);
    m_prepared = true;
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

bool HelperInstallSession::commit(QString* error) {
    if (!m_prepared) {
        if (error != nullptr) {
            *error = QStringLiteral("Install helper was not prepared.");
        }
        return false;
    }
    const QString script = m_stage.path() + QStringLiteral("/install_system_files.py");
    return runPkexec({script, QStringLiteral("install"), m_stage.path()}, error);
}

QString HelperInstallSession::prompt() const {
    return m_prompt;
}

QString HelperInstallSession::stagePath() const {
    return m_prepared ? m_stage.path() : QString();
}

bool HelperUninstallSession::prepare(const AppImageLaunch& launch, QString* error) {
    m_prepared = false;
    m_prompt.clear();
    if (!m_stage.isValid()) {
        if (error != nullptr) {
            *error = QStringLiteral("Could not create a temporary directory.");
        }
        return false;
    }
    if (!launch.active) {
        if (error != nullptr) {
            *error =
                QStringLiteral("Uninstall helper is only available when running the AppImage.");
        }
        return false;
    }
    const SystemHelperStatus status = inspectSystemHelper({}, true);
    if (!status.managedByApp) {
        if (error != nullptr) {
            *error = QStringLiteral("There is no AppImage helper install to remove.");
        }
        return false;
    }
    QString readError;
    const QString body = readTextFile(manifestDestination(), kMaxPolicyBytes, &readError);
    QList<ManifestEntry> entries;
    if (!parseManifest(body, &entries, &readError)) {
        if (error != nullptr) {
            *error = readError;
        }
        return false;
    }
    const QString scriptSource =
        launch.appDir + QStringLiteral("/usr/share/controld-tray/install_system_files.py");
    const QString stagedScript = m_stage.path() + QStringLiteral("/install_system_files.py");
    if (!stageFile(launch.appDir, scriptSource, stagedScript, error)) {
        return false;
    }
    QStringList destinations;
    for (const ManifestEntry& entry : entries) {
        destinations.append(entry.destination);
    }
    destinations.append(manifestDestination());
    m_prompt = helperUninstallPrompt(destinations);
    m_prepared = true;
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

bool HelperUninstallSession::commit(QString* error) {
    if (!m_prepared) {
        if (error != nullptr) {
            *error = QStringLiteral("Uninstall helper was not prepared.");
        }
        return false;
    }
    const QString script = m_stage.path() + QStringLiteral("/install_system_files.py");
    return runPkexec({script, QStringLiteral("uninstall")}, error);
}

QString HelperUninstallSession::prompt() const {
    return m_prompt;
}

} // namespace controld
