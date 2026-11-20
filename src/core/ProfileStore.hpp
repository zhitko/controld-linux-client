#pragma once

#include "core/Types.hpp"

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

class QSettings;

namespace controld {

class ProfileStore {
  public:
    explicit ProfileStore(QString filePath);

    QString filePath() const;
    QStringList loadWarnings() const;
    QList<Profile> profiles() const;
    std::optional<Profile> profile(const QString& id) const;

    QString addProfile(const QString& name, const QString& resolverId, QString* error);
    bool updateProfile(const QString& id, const QString& name, const QString& resolverId,
                       QString* error);
    bool removeProfile(const QString& id, QString* error);

    QString activeProfileId() const;
    bool setActiveProfileId(const QString& id, QString* error = nullptr);

    QList<int> appliedIfindexes() const;
    bool setAppliedIfindexes(const QList<int>& ifindexes, QString* error = nullptr);

    bool chromePolicyEnabled() const;
    bool setChromePolicyEnabled(bool enabled, QString* error = nullptr);

    QString chromePinnedResolverId() const;
    bool setChromePinnedResolverId(const QString& resolverId, QString* error = nullptr);

    bool chromePolicyDirty() const;
    bool setChromePolicyDirty(bool dirty, QString* error = nullptr);

  private:
    void load();
    bool write(QString* error);
    template <typename T> bool persistValue(T& field, const T& value, QString* error) {
        const T previous = field;
        field = value;
        if (!write(error)) {
            field = previous;
            return false;
        }
        return true;
    }
    static QList<int> sanitizeIfindexes(const QList<int>& ifindexes);

    QString m_path;
    QList<Profile> m_profiles;
    QStringList m_warnings;
    QString m_activeProfileId;
    QList<int> m_appliedIfindexes;
    bool m_chromePolicyEnabled = false;
    QString m_chromePinnedResolverId;
    bool m_chromePolicyDirty = false;
};

} // namespace controld
