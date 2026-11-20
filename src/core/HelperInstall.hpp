#pragma once

#include "core/AppImageLaunch.hpp"

#include <QList>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

namespace controld {

QString helperDestination();
QString policyDestination();
QString libraryDirectory();
QString manifestDestination();
QString policyActionId();
QString dnsPolicyActionId();
bool policyHasDnsAuthorization(const QString& text);

bool installDestinationAllowed(const QString& destination);
bool policyTextAcceptable(const QString& text);

struct ManifestEntry {
    QString mode;
    QString destination;
    QString sourceName;
    QString sha256;
    bool operator==(const ManifestEntry& other) const;
};

bool parseManifest(const QString& text, QList<ManifestEntry>* entries, QString* error);
QString manifestText(const QList<ManifestEntry>& entries);

struct LddEntry {
    QString soname;
    QString resolvedPath;
    bool found = false;
};

QList<LddEntry> parseLddOutput(const QString& text);
bool isHostRuntimeLibrary(const QString& soname);

struct BundledLibrary {
    QString soname;
    QString sourcePath;
};

struct LibraryPlan {
    bool ok = false;
    QString error;
    QList<BundledLibrary> libraries;
};

LibraryPlan planBundledLibraries(const QList<LddEntry>& entries, const QString& appDir);
LibraryPlan librariesForHelper(const QString& helperPath, const QString& appDir);

struct SystemHelperStatus {
    bool ready = false;
    bool managedByApp = false;
    bool groupedDnsAuthorization = false;
    QString detail;
};

SystemHelperStatus inspectSystemHelper(const QString& rootPrefix, bool requireRootOwner);
QString appImageHelperNote(const SystemHelperStatus& status);
QString helperInstallPrompt(const QStringList& destinations);
QString helperUninstallPrompt(const QStringList& destinations);
bool privilegedCopyToolsReady(QString* error);

class HelperInstallSession {
  public:
    bool prepare(const AppImageLaunch& launch, QString* error);
    bool commit(QString* error);
    QString prompt() const;
    QString stagePath() const;

  private:
    QTemporaryDir m_stage;
    bool m_prepared = false;
    QString m_prompt;
};

class HelperUninstallSession {
  public:
    bool prepare(const AppImageLaunch& launch, QString* error);
    bool commit(QString* error);
    QString prompt() const;

  private:
    QTemporaryDir m_stage;
    bool m_prepared = false;
    QString m_prompt;
};

} // namespace controld
