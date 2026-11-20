#include "core/ChromePolicy.hpp"

#include "core/ControlD.hpp"
#include "core/Validation.hpp"

#include <QJsonDocument>
#include <QJsonObject>

namespace controld {

QStringList ChromePolicy::managedPolicyPaths() {
    return {
        QStringLiteral("/etc/opt/chrome/policies/managed/controld.json"),
        QStringLiteral("/etc/chromium/policies/managed/controld.json"),
    };
}

QByteArray ChromePolicy::document(const QString& resolverId, QString* error) {
    const QString idError = resolverIdError(resolverId);
    if (!idError.isEmpty()) {
        if (error != nullptr) {
            *error = idError;
        }
        return {};
    }

    QJsonObject object;
    object.insert(QStringLiteral("DnsOverHttpsMode"), QStringLiteral("secure"));
    object.insert(QStringLiteral("DnsOverHttpsTemplates"), ControlD::dohTemplate(resolverId));
    const QByteArray json = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (error != nullptr) {
        error->clear();
    }
    return json;
}

} // namespace controld
