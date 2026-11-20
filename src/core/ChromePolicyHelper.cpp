#include "core/ChromePolicyHelper.hpp"

#include "core/ChromePolicy.hpp"
#include "core/Validation.hpp"

#include <QDir>
#include <QFileInfo>
#include <QUuid>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace controld {
namespace {

HelperOutcome outcome(int code, QString error) {
    HelperOutcome result;
    result.code = code;
    result.error = std::move(error);
    return result;
}

int openDirectoryChain(const QString& dirPath, bool create, bool* missing, QString* error) {
    *missing = false;
    if (!dirPath.startsWith(QLatin1Char('/'))) {
        *error = QStringLiteral("Policy directory is not absolute.");
        return -1;
    }
    int fd = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) {
        *error = QString::fromLocal8Bit(strerror(errno));
        return -1;
    }
    QString current;
    const QStringList parts = dirPath.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        if (part == QLatin1String(".") || part == QLatin1String("..")) {
            *error = QStringLiteral("Policy path contains a relative component.");
            close(fd);
            return -1;
        }
        current += QLatin1Char('/') + part;

        const QByteArray component = part.toUtf8();
        int next =
            openat(fd, component.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (next < 0 && errno == ENOENT && create) {
            if (mkdirat(fd, component.constData(), 0755) != 0 && errno != EEXIST) {
                *error = QStringLiteral("Could not create %1: %2")
                             .arg(current, QString::fromLocal8Bit(strerror(errno)));
                close(fd);
                return -1;
            }
            next =
                openat(fd, component.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        }
        if (next < 0) {
            *missing = errno == ENOENT && !create;
            if (!*missing) {
                *error = QStringLiteral("Could not safely open directory %1: %2")
                             .arg(current, QString::fromLocal8Bit(strerror(errno)));
            }
            close(fd);
            return -1;
        }
        close(fd);
        fd = next;
    }
    return fd;
}

bool writeAtomic(const QString& path, const QByteArray& data, QString* error) {
    const QFileInfo info(path);
    bool missing = false;
    const int directory = openDirectoryChain(info.dir().absolutePath(), true, &missing, error);
    if (directory < 0) {
        return false;
    }
    // All operations below are relative to the validated directory descriptor.
    const QByteArray name = info.fileName().toUtf8();

    struct stat status {};
    if (fstatat(directory, name.constData(), &status, AT_SYMLINK_NOFOLLOW) == 0) {
        if (S_ISLNK(status.st_mode)) {
            *error = QStringLiteral("Refusing to replace a symlink: %1").arg(path);
            close(directory);
            return false;
        }
        if (!S_ISREG(status.st_mode)) {
            *error = QStringLiteral("Refusing to replace a non-regular file: %1").arg(path);
            close(directory);
            return false;
        }
    } else if (errno != ENOENT) {
        *error = QStringLiteral("Could not inspect %1: %2")
                     .arg(path, QString::fromLocal8Bit(strerror(errno)));
        close(directory);
        return false;
    }

    const QByteArray tempPath =
        name + "." + QUuid::createUuid().toByteArray(QUuid::WithoutBraces) + ".tmp";
    const int fd = openat(directory, tempPath.constData(),
                          O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
    if (fd < 0) {
        *error = QStringLiteral("Could not create %1: %2")
                     .arg(QString::fromUtf8(tempPath), QString::fromLocal8Bit(strerror(errno)));
        close(directory);
        return false;
    }

    auto closeFd = [&]() {
        if (fd >= 0) {
            close(fd);
        }
    };
    auto discard = [&]() {
        unlinkat(directory, tempPath.constData(), 0);
        close(directory);
    };

    if (fchmod(fd, 0644) != 0) {
        *error = QStringLiteral("Could not set permissions on %1: %2")
                     .arg(QString::fromUtf8(tempPath), QString::fromLocal8Bit(strerror(errno)));
        closeFd();
        discard();
        return false;
    }

    qint64 offset = 0;
    while (offset < data.size()) {
        const ssize_t wrote =
            ::write(fd, data.constData() + offset, static_cast<size_t>(data.size() - offset));
        if (wrote < 0 && errno == EINTR) {
            continue;
        }
        if (wrote <= 0) {
            *error = QStringLiteral("Could not write %1: %2")
                         .arg(path, QString::fromLocal8Bit(strerror(errno)));
            closeFd();
            discard();
            return false;
        }
        offset += wrote;
    }
    if (fsync(fd) != 0) {
        *error = QStringLiteral("Could not flush %1: %2")
                     .arg(path, QString::fromLocal8Bit(strerror(errno)));
        closeFd();
        discard();
        return false;
    }
    if (close(fd) != 0) {
        *error = QStringLiteral("Could not close %1: %2")
                     .arg(path, QString::fromLocal8Bit(strerror(errno)));
        discard();
        return false;
    }
    if (renameat(directory, tempPath.constData(), directory, name.constData()) != 0) {
        *error = QStringLiteral("Could not replace %1: %2")
                     .arg(path, QString::fromLocal8Bit(strerror(errno)));
        discard();
        return false;
    }
    close(directory);
    return true;
}

bool removeFile(const QString& path, QString* error) {
    const QFileInfo info(path);
    bool missing = false;
    const int directory = openDirectoryChain(info.dir().absolutePath(), false, &missing, error);
    if (directory < 0) {
        return missing;
    }
    const QByteArray name = info.fileName().toUtf8();
    struct stat status {};
    if (fstatat(directory, name.constData(), &status, AT_SYMLINK_NOFOLLOW) != 0) {
        if (errno == ENOENT) {
            close(directory);
            return true;
        }
        *error = QStringLiteral("Could not inspect %1: %2")
                     .arg(path, QString::fromLocal8Bit(strerror(errno)));
        close(directory);
        return false;
    }
    if (S_ISLNK(status.st_mode)) {
        *error = QStringLiteral("Refusing to remove a symlink: %1").arg(path);
        close(directory);
        return false;
    }
    if (!S_ISREG(status.st_mode)) {
        *error = QStringLiteral("Refusing to remove a non-regular file: %1").arg(path);
        close(directory);
        return false;
    }
    if (unlinkat(directory, name.constData(), 0) != 0) {
        *error = QStringLiteral("Could not remove %1: %2")
                     .arg(path, QString::fromLocal8Bit(strerror(errno)));
        close(directory);
        return false;
    }
    close(directory);
    return true;
}

bool pathsAreAbsolute(const QStringList& files, QString* error) {
    if (files.isEmpty()) {
        *error = QStringLiteral("No policy files were configured.");
        return false;
    }
    for (const QString& file : files) {
        if (!file.startsWith(QLatin1Char('/'))) {
            *error = QStringLiteral("Policy path is not absolute.");
            return false;
        }
        const QStringList parts = file.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        if (parts.contains(QStringLiteral(".")) || parts.contains(QStringLiteral(".."))) {
            *error = QStringLiteral("Policy path contains a relative component.");
            return false;
        }
    }
    return true;
}

} // namespace

HelperOutcome runChromePolicyHelper(const QStringList& args, const QStringList& files) {
    QString error;
    if (!pathsAreAbsolute(files, &error)) {
        return outcome(1, error);
    }

    if (args.size() == 1 && args.at(0) == QLatin1String("clear")) {
        for (const QString& file : files) {
            if (!removeFile(file, &error)) {
                return outcome(2, error);
            }
        }
        return outcome(0, {});
    }

    if (args.size() == 2 && args.at(0) == QLatin1String("apply")) {
        const QString& resolverId = args.at(1);
        if (!isValidResolverId(resolverId)) {
            return outcome(1, resolverIdError(resolverId));
        }
        const QByteArray document = ChromePolicy::document(resolverId, &error);
        if (document.isEmpty()) {
            return outcome(1, error.isEmpty()
                                  ? QStringLiteral("Could not build the policy document.")
                                  : error);
        }
        for (const QString& file : files) {
            if (!writeAtomic(file, document, &error)) {
                return outcome(2, error);
            }
        }
        return outcome(0, {});
    }

    return outcome(
        1, QStringLiteral("Usage: controld-tray-chrome-policy apply <resolver-id> | clear"));
}

} // namespace controld
