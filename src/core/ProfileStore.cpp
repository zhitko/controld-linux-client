#include "core/ProfileStore.hpp"

#include "core/Validation.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QUuid>

namespace controld {
namespace {

QString newProfileId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

} // namespace

ProfileStore::ProfileStore(QString filePath) : m_path(std::move(filePath)) {
    load();
}

QString ProfileStore::filePath() const {
    return m_path;
}

QStringList ProfileStore::loadWarnings() const {
    return m_warnings;
}

QList<Profile> ProfileStore::profiles() const {
    return m_profiles;
}

std::optional<Profile> ProfileStore::profile(const QString& id) const {
    for (const Profile& profile : m_profiles) {
        if (profile.id == id) {
            return profile;
        }
    }
    return std::nullopt;
}

QString ProfileStore::addProfile(const QString& name, const QString& resolverId, QString* error) {
    const QString trimmedName = normalizedProfileName(name);
    const QString nameError = profileNameError(trimmedName);
    if (!nameError.isEmpty()) {
        if (error != nullptr) {
            *error = nameError;
        }
        return {};
    }
    const QString trimmedId = resolverId.trimmed();
    const QString idError = resolverIdError(trimmedId);
    if (!idError.isEmpty()) {
        if (error != nullptr) {
            *error = idError;
        }
        return {};
    }

    Profile profile;
    profile.id = newProfileId();
    profile.name = trimmedName;
    profile.resolverId = trimmedId;
    m_profiles.append(profile);
    write();
    if (error != nullptr) {
        error->clear();
    }
    return profile.id;
}

bool ProfileStore::updateProfile(const QString& id, const QString& name, const QString& resolverId,
                                 QString* error) {
    const QString trimmedName = normalizedProfileName(name);
    const QString nameError = profileNameError(trimmedName);
    if (!nameError.isEmpty()) {
        if (error != nullptr) {
            *error = nameError;
        }
        return false;
    }
    const QString trimmedId = resolverId.trimmed();
    const QString idError = resolverIdError(trimmedId);
    if (!idError.isEmpty()) {
        if (error != nullptr) {
            *error = idError;
        }
        return false;
    }

    for (Profile& profile : m_profiles) {
        if (profile.id != id) {
            continue;
        }
        profile.name = trimmedName;
        profile.resolverId = trimmedId;
        write();
        if (error != nullptr) {
            error->clear();
        }
        return true;
    }
    if (error != nullptr) {
        *error = QStringLiteral("Profile was not found.");
    }
    return false;
}

bool ProfileStore::removeProfile(const QString& id, QString* error) {
    for (int index = 0; index < m_profiles.size(); ++index) {
        if (m_profiles.at(index).id != id) {
            continue;
        }
        m_profiles.removeAt(index);
        if (m_activeProfileId == id) {
            m_activeProfileId.clear();
        }
        write();
        if (error != nullptr) {
            error->clear();
        }
        return true;
    }
    if (error != nullptr) {
        *error = QStringLiteral("Profile was not found.");
    }
    return false;
}

QString ProfileStore::activeProfileId() const {
    return m_activeProfileId;
}

void ProfileStore::setActiveProfileId(const QString& id) {
    if (m_activeProfileId == id) {
        return;
    }
    m_activeProfileId = id;
    write();
}

QList<int> ProfileStore::appliedIfindexes() const {
    return m_appliedIfindexes;
}

void ProfileStore::setAppliedIfindexes(const QList<int>& ifindexes) {
    const QList<int> sanitized = sanitizeIfindexes(ifindexes);
    if (sanitized == m_appliedIfindexes) {
        return;
    }
    m_appliedIfindexes = sanitized;
    write();
}

bool ProfileStore::chromePolicyEnabled() const {
    return m_chromePolicyEnabled;
}

void ProfileStore::setChromePolicyEnabled(bool enabled) {
    if (m_chromePolicyEnabled == enabled) {
        return;
    }
    m_chromePolicyEnabled = enabled;
    write();
}

QString ProfileStore::chromePinnedResolverId() const {
    return m_chromePinnedResolverId;
}

void ProfileStore::setChromePinnedResolverId(const QString& resolverId) {
    if (m_chromePinnedResolverId == resolverId) {
        return;
    }
    m_chromePinnedResolverId = resolverId;
    write();
}

QList<int> ProfileStore::sanitizeIfindexes(const QList<int>& ifindexes) {
    QList<int> sanitized;
    for (const int ifindex : ifindexes) {
        if (ifindex <= 0 || sanitized.contains(ifindex)) {
            continue;
        }
        sanitized.append(ifindex);
        if (sanitized.size() == 32) {
            break;
        }
    }
    return sanitized;
}

void ProfileStore::load() {
    m_warnings.clear();
    m_profiles.clear();
    bool repaired = false;
    bool unreadable = false;
    {
        QSettings settings(m_path, QSettings::IniFormat);
        const int count = settings.beginReadArray(QStringLiteral("profiles"));
        for (int index = 0; index < count; ++index) {
            settings.setArrayIndex(index);
            Profile profile;
            profile.id = settings.value(QStringLiteral("id")).toString().trimmed();
            profile.name = normalizedProfileName(settings.value(QStringLiteral("name")).toString());
            profile.resolverId = settings.value(QStringLiteral("resolverId")).toString().trimmed();
            if (profile.id.isEmpty() || !profileNameError(profile.name).isEmpty() ||
                !isValidResolverId(profile.resolverId)) {
                m_warnings.append(
                    QStringLiteral("Skipped a stored profile that failed validation."));
                repaired = true;
                continue;
            }
            bool duplicate = false;
            for (const Profile& existing : m_profiles) {
                if (existing.id == profile.id) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) {
                m_warnings.append(QStringLiteral("Skipped a stored profile with a duplicate id."));
                repaired = true;
                continue;
            }
            m_profiles.append(profile);
        }
        settings.endArray();

        m_activeProfileId = settings.value(QStringLiteral("general/activeProfileId")).toString();
        if (!m_activeProfileId.isEmpty() && !profile(m_activeProfileId).has_value()) {
            m_warnings.append(QStringLiteral("Cleared an active profile id that does not exist."));
            m_activeProfileId.clear();
            repaired = true;
        }

        QList<int> loadedIfindexes;
        const QStringList storedIfindexes =
            settings.value(QStringLiteral("general/appliedIfindexes")).toStringList();
        for (const QString& text : storedIfindexes) {
            bool ok = false;
            const int ifindex = text.toInt(&ok);
            if (ok) {
                loadedIfindexes.append(ifindex);
            } else {
                repaired = true;
            }
        }
        m_appliedIfindexes = sanitizeIfindexes(loadedIfindexes);
        if (m_appliedIfindexes.size() != loadedIfindexes.size()) {
            repaired = true;
        }

        m_chromePolicyEnabled =
            settings.value(QStringLiteral("general/chromePolicyEnabled"), false).toBool();
        m_chromePinnedResolverId =
            settings.value(QStringLiteral("general/chromePinnedResolverId")).toString();
        if (!m_chromePinnedResolverId.isEmpty() && !isValidResolverId(m_chromePinnedResolverId)) {
            m_warnings.append(
                QStringLiteral("Cleared a stored Chrome resolver ID that failed validation."));
            m_chromePinnedResolverId.clear();
            repaired = true;
        }
        unreadable = settings.status() != QSettings::NoError;
    }

    if (unreadable) {
        m_warnings.append(QStringLiteral("Settings could not be read."));
    }
    if (repaired) {
        write();
    }
}

void ProfileStore::write() {
    const QFileInfo info(m_path);
    QDir().mkpath(info.dir().absolutePath());

    QSettings settings(m_path, QSettings::IniFormat);
    settings.clear();
    settings.beginWriteArray(QStringLiteral("profiles"));
    for (int index = 0; index < m_profiles.size(); ++index) {
        settings.setArrayIndex(index);
        settings.setValue(QStringLiteral("id"), m_profiles.at(index).id);
        settings.setValue(QStringLiteral("name"), m_profiles.at(index).name);
        settings.setValue(QStringLiteral("resolverId"), m_profiles.at(index).resolverId);
    }
    settings.endArray();
    settings.setValue(QStringLiteral("general/activeProfileId"), m_activeProfileId);
    QStringList ifindexes;
    for (const int ifindex : m_appliedIfindexes) {
        ifindexes.append(QString::number(ifindex));
    }
    settings.setValue(QStringLiteral("general/appliedIfindexes"), ifindexes);
    settings.setValue(QStringLiteral("general/chromePolicyEnabled"), m_chromePolicyEnabled);
    settings.setValue(QStringLiteral("general/chromePinnedResolverId"), m_chromePinnedResolverId);
    settings.sync();
    secureFile();
}

void ProfileStore::secureFile() const {
    if (!QFile::exists(m_path)) {
        return;
    }
    QFile::setPermissions(m_path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

} // namespace controld
