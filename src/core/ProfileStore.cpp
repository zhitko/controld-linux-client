#include "core/ProfileStore.hpp"

#include "core/Validation.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>
#include <QTemporaryFile>
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
    if (!write(error)) {
        m_profiles.removeLast();
        return {};
    }
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
        const Profile previous = profile;
        profile.name = trimmedName;
        profile.resolverId = trimmedId;
        if (!write(error)) {
            profile = previous;
            return false;
        }
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
        const Profile previous = m_profiles.takeAt(index);
        const QString previousActive = m_activeProfileId;
        if (m_activeProfileId == id) {
            m_activeProfileId.clear();
        }
        if (!write(error)) {
            m_profiles.insert(index, previous);
            m_activeProfileId = previousActive;
            return false;
        }
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

bool ProfileStore::setActiveProfileId(const QString& id, QString* error) {
    return persistValue(m_activeProfileId, id, error);
}

QList<int> ProfileStore::appliedIfindexes() const {
    return m_appliedIfindexes;
}

bool ProfileStore::setAppliedIfindexes(const QList<int>& ifindexes, QString* error) {
    return persistValue(m_appliedIfindexes, sanitizeIfindexes(ifindexes), error);
}

bool ProfileStore::chromePolicyEnabled() const {
    return m_chromePolicyEnabled;
}

bool ProfileStore::setChromePolicyEnabled(bool enabled, QString* error) {
    return persistValue(m_chromePolicyEnabled, enabled, error);
}

QString ProfileStore::chromePinnedResolverId() const {
    return m_chromePinnedResolverId;
}

bool ProfileStore::setChromePinnedResolverId(const QString& resolverId, QString* error) {
    return persistValue(m_chromePinnedResolverId, resolverId, error);
}

bool ProfileStore::chromePolicyDirty() const {
    return m_chromePolicyDirty;
}

bool ProfileStore::setChromePolicyDirty(bool dirty, QString* error) {
    return persistValue(m_chromePolicyDirty, dirty, error);
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
        settings.sync();
        // QSettings serializes the reserved "general" section as [%General].
        // On a fresh read its keys are under "General", regardless of input case.
        auto general = [&](const QString& key, const QVariant& fallback = QVariant()) {
            return settings.value(QStringLiteral("General/") + key,
                                  settings.value(QStringLiteral("general/") + key, fallback));
        };
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

        m_activeProfileId = general(QStringLiteral("activeProfileId")).toString();
        if (!m_activeProfileId.isEmpty() && !profile(m_activeProfileId).has_value()) {
            m_warnings.append(QStringLiteral("Cleared an active profile id that does not exist."));
            m_activeProfileId.clear();
            repaired = true;
        }

        QList<int> loadedIfindexes;
        const QStringList storedIfindexes =
            general(QStringLiteral("appliedIfindexes")).toStringList();
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

        m_chromePolicyEnabled = general(QStringLiteral("chromePolicyEnabled"), false).toBool();
        m_chromePinnedResolverId = general(QStringLiteral("chromePinnedResolverId")).toString();
        m_chromePolicyDirty = general(QStringLiteral("chromePolicyDirty"), false).toBool();
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
        QString error;
        if (!write(&error)) {
            m_warnings.append(error);
        }
    }
}

bool ProfileStore::write(QString* error) {
    auto failed = [&](const QString& detail) {
        if (error != nullptr) {
            *error = QStringLiteral("Could not save settings to %1: %2").arg(m_path, detail);
        }
        return false;
    };
    const QFileInfo info(m_path);
    if (!QDir().mkpath(info.dir().absolutePath())) {
        return failed(QStringLiteral("could not create the settings directory"));
    }
    QTemporaryFile staged(info.dir().filePath(QStringLiteral(".controld-settings-XXXXXX")));
    if (!staged.open()) {
        return failed(staged.errorString());
    }
    const QString stagedPath = staged.fileName();
    staged.close();

    {
        QSettings settings(stagedPath, QSettings::IniFormat);
        settings.clear();
        settings.beginWriteArray(QStringLiteral("profiles"));
        for (int index = 0; index < m_profiles.size(); ++index) {
            settings.setArrayIndex(index);
            settings.setValue(QStringLiteral("id"), m_profiles.at(index).id);
            settings.setValue(QStringLiteral("name"), m_profiles.at(index).name);
            settings.setValue(QStringLiteral("resolverId"), m_profiles.at(index).resolverId);
        }
        settings.endArray();
        settings.setValue(QStringLiteral("General/activeProfileId"), m_activeProfileId);
        QStringList ifindexes;
        for (const int ifindex : m_appliedIfindexes) {
            ifindexes.append(QString::number(ifindex));
        }
        settings.setValue(QStringLiteral("General/appliedIfindexes"), ifindexes);
        settings.setValue(QStringLiteral("General/chromePolicyEnabled"), m_chromePolicyEnabled);
        settings.setValue(QStringLiteral("General/chromePinnedResolverId"),
                          m_chromePinnedResolverId);
        settings.setValue(QStringLiteral("General/chromePolicyDirty"), m_chromePolicyDirty);
        settings.sync();
        if (settings.status() != QSettings::NoError) {
            return failed(QStringLiteral("could not serialize settings"));
        }
    }
    QFile serialized(stagedPath);
    if (!serialized.open(QIODevice::ReadOnly)) {
        return failed(serialized.errorString());
    }
    const QByteArray bytes = serialized.readAll();
    if (serialized.error() != QFileDevice::NoError) {
        return failed(serialized.errorString());
    }
    QSaveFile destination(m_path);
    if (!destination.open(QIODevice::WriteOnly) ||
        !destination.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
        destination.write(bytes) != bytes.size() || !destination.commit()) {
        return failed(destination.errorString());
    }
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

} // namespace controld
