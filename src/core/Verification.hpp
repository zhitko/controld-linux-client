#pragma once

#include "core/Types.hpp"

#include <QString>

#include <cstdint>

namespace controld {

class Verification {
  public:
    static constexpr const char* queryName() {
        return "verify.controld.com";
    }

    static constexpr const char* expectedCanonical() {
        return "api.controld.com";
    }

    static quint64 queryFlags();
    static QString normalizeName(const QString& name);
    static bool canonicalMatches(const QString& canonicalName);
    static bool passed(const ResolveResult& result);
    static QString failureDetail(const ResolveResult& result);
};

} // namespace controld
