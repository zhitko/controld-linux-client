#include "core/Types.hpp"

namespace controld {

CallResult CallResult::success() {
    return {};
}

CallResult CallResult::failure(QString name, QString message) {
    CallResult result;
    result.ok = false;
    result.errorName = std::move(name);
    result.errorMessage = std::move(message);
    return result;
}

QString CallResult::summary() const {
    if (ok) {
        return {};
    }
    if (errorName.isEmpty()) {
        return errorMessage;
    }
    if (errorMessage.isEmpty()) {
        return errorName;
    }
    return errorName + QStringLiteral(": ") + errorMessage;
}

QString ResolveResult::errorSummary() const {
    if (errorName.isEmpty()) {
        return errorMessage;
    }
    if (errorMessage.isEmpty()) {
        return errorName;
    }
    return errorName + QStringLiteral(": ") + errorMessage;
}

} // namespace controld
