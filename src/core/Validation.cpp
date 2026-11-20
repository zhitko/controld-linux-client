#include "core/Validation.hpp"

namespace controld {
namespace {

bool isAsciiAlphanumeric(char16_t code) {
    const bool digit = code >= u'0' && code <= u'9';
    const bool upper = code >= u'A' && code <= u'Z';
    const bool lower = code >= u'a' && code <= u'z';
    return digit || upper || lower;
}

} // namespace

QString resolverIdError(const QString& resolverId) {
    if (resolverId.isEmpty()) {
        return QStringLiteral("Resolver ID is empty.");
    }
    if (resolverId.size() > 64) {
        return QStringLiteral("Resolver ID must be at most 64 characters.");
    }
    for (const QChar character : resolverId) {
        if (!isAsciiAlphanumeric(character.unicode())) {
            return QStringLiteral("Resolver ID must contain only ASCII letters and digits.");
        }
    }
    return {};
}

bool isValidResolverId(const QString& resolverId) {
    return resolverIdError(resolverId).isEmpty();
}

QString normalizedProfileName(const QString& name) {
    return name.trimmed();
}

QString profileNameError(const QString& name) {
    const QString trimmed = normalizedProfileName(name);
    if (trimmed.isEmpty()) {
        return QStringLiteral("Profile name is empty.");
    }
    if (trimmed.size() > 80) {
        return QStringLiteral("Profile name must be at most 80 characters.");
    }
    for (const QChar character : trimmed) {
        if (character.category() == QChar::Other_Control || character.unicode() == 0x7F) {
            return QStringLiteral("Profile name contains a control character.");
        }
    }
    return {};
}

} // namespace controld
