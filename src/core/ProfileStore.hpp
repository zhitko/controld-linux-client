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
    void setActiveProfileId(const QString& id);

    QList<int> appliedIfindexes() const;
    void setAppliedIfindexes(const QList<int>& ifindexes);

    bool chromePolicyEnabled() const;
    void setChromePolicyEnabled(bool enabled);

    QString chromePinnedResolverId() const;
    void setChromePinnedResolverId(const QString& resolverId);

  private:
    void load();
    void write();
    void secureFile() const;
    static QList<int> sanitizeIfindexes(const QList<int>& ifindexes);

    QString m_path;
    QList<Profile> m_profiles;
    QStringList m_warnings;
    QString m_activeProfileId;
    QList<int> m_appliedIfindexes;
    bool m_chromePolicyEnabled = false;
    QString m_chromePinnedResolverId;
};

} // namespace controld
