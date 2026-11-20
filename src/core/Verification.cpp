#include "core/Verification.hpp"

namespace controld {

quint64 Verification::queryFlags() {
    // SD_RESOLVED_DNS | SD_RESOLVED_NO_SEARCH | SD_RESOLVED_NO_CACHE
    constexpr quint64 dns = 1ULL << 0;
    constexpr quint64 noSearch = 1ULL << 8;
    constexpr quint64 noCache = 1ULL << 12;
    return dns | noSearch | noCache;
}

QString Verification::normalizeName(const QString& name) {
    QString normalized = name.trimmed().toLower();
    if (normalized.endsWith(QLatin1Char('.'))) {
        normalized.chop(1);
    }
    return normalized;
}

bool Verification::canonicalMatches(const QString& canonicalName) {
    return normalizeName(canonicalName) == QString::fromLatin1(expectedCanonical());
}

bool Verification::passed(const ResolveResult& result) {
    return result.ok && !result.addresses.isEmpty() && canonicalMatches(result.canonicalName);
}

QString Verification::failureDetail(const ResolveResult& result) {
    const QString name = QString::fromLatin1(queryName());
    if (!result.ok) {
        const QString why = result.errorSummary();
        if (why.isEmpty()) {
            return QStringLiteral("Lookup of %1 failed.").arg(name);
        }
        return QStringLiteral("Lookup of %1 failed: %2").arg(name, why);
    }
    if (result.addresses.isEmpty()) {
        return QStringLiteral("%1 returned no addresses.").arg(name);
    }
    const QString got =
        result.canonicalName.isEmpty() ? QStringLiteral("(empty)") : result.canonicalName;
    return QStringLiteral("%1 canonical name was \"%2\", expected \"%3\".")
        .arg(name, got, QString::fromLatin1(expectedCanonical()));
}

} // namespace controld
