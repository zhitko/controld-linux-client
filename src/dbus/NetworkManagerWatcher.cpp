#include "dbus/NetworkManagerWatcher.hpp"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusVariant>
#include <QtEndian>

#include <net/if.h>

namespace controld {
namespace {

constexpr int kRefreshDelayMs = 250;
constexpr int kPropertyTimeoutMs = 8000;
constexpr uint kDeviceActivated = 100;
constexpr uint kConnectionActivated = 2;

QVariant unwrap(const QVariant& value) {
    if (value.userType() == qMetaTypeId<QDBusVariant>()) {
        return value.value<QDBusVariant>().variant();
    }
    return value;
}

QVariant readProperty(const QString& path, const QString& interface, const QString& name,
                      QString* error) {
    QDBusMessage message = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.NetworkManager"), path,
        QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    message << interface << name;
    const QDBusMessage reply =
        QDBusConnection::systemBus().call(message, QDBus::BlockWithGui, kPropertyTimeoutMs);
    if (reply.type() != QDBusMessage::ReplyMessage) {
        if (error != nullptr) {
            const QString text =
                reply.errorMessage().isEmpty() ? reply.errorName() : reply.errorMessage();
            *error = text.isEmpty() ? QStringLiteral("NetworkManager did not answer.") : text;
        }
        return {};
    }
    if (reply.arguments().isEmpty()) {
        return {};
    }
    return unwrap(reply.arguments().at(0));
}

QString objectPath(const QVariant& value) {
    if (value.canConvert<QDBusObjectPath>()) {
        return value.value<QDBusObjectPath>().path();
    }
    return value.toString();
}

QStringList objectPaths(const QVariant& value) {
    QStringList paths;
    if (value.canConvert<QDBusArgument>()) {
        const QDBusArgument argument = value.value<QDBusArgument>();
        if (argument.currentType() == QDBusArgument::ArrayType) {
            argument.beginArray();
            while (!argument.atEnd()) {
                QDBusObjectPath path;
                argument >> path;
                paths.append(path.path());
            }
            argument.endArray();
            return paths;
        }
    }
    const QList<QDBusObjectPath> typed = value.value<QList<QDBusObjectPath>>();
    for (const QDBusObjectPath& path : typed) {
        paths.append(path.path());
    }
    return paths;
}

QVariant dbusField(const QVariant& stored) {
    if (stored.userType() == qMetaTypeId<QDBusVariant>()) {
        return stored.value<QDBusVariant>().variant();
    }
    return stored;
}

DnsServerEndpoint endpointFromMap(const QVariantMap& entry) {
    DnsServerEndpoint endpoint;
    endpoint.address = QHostAddress(dbusField(entry.value(QStringLiteral("address"))).toString());
    endpoint.port = static_cast<quint16>(dbusField(entry.value(QStringLiteral("port"))).toUInt());
    return endpoint;
}

QList<DnsServerEndpoint> serversFromNameserverData(const QVariant& value) {
    QList<DnsServerEndpoint> servers;
    if (value.canConvert<QDBusArgument>()) {
        const QDBusArgument argument = value.value<QDBusArgument>();
        if (argument.currentType() != QDBusArgument::ArrayType) {
            return {};
        }
        argument.beginArray();
        while (!argument.atEnd()) {
            QVariantMap entry;
            argument >> entry;
            const DnsServerEndpoint endpoint = endpointFromMap(entry);
            if (!endpoint.address.isNull()) {
                servers.append(endpoint);
            }
        }
        argument.endArray();
        return servers;
    }
    for (const QVariant& item : value.toList()) {
        const DnsServerEndpoint endpoint = endpointFromMap(item.toMap());
        if (!endpoint.address.isNull()) {
            servers.append(endpoint);
        }
    }
    return servers;
}

QList<DnsServerEndpoint> serversFromIpv4Nameservers(const QVariant& value) {
    if (!value.canConvert<QDBusArgument>()) {
        return {};
    }
    const QDBusArgument argument = value.value<QDBusArgument>();
    if (argument.currentType() != QDBusArgument::ArrayType) {
        return {};
    }
    QList<DnsServerEndpoint> servers;
    argument.beginArray();
    while (!argument.atEnd()) {
        quint32 raw = 0;
        argument >> raw;
        DnsServerEndpoint endpoint;
        endpoint.address = QHostAddress(qFromBigEndian(raw));
        if (!endpoint.address.isNull()) {
            servers.append(endpoint);
        }
    }
    argument.endArray();
    return servers;
}

QStringList stringListFromValue(const QVariant& value) {
    if (value.canConvert<QStringList>()) {
        return value.toStringList();
    }
    if (!value.canConvert<QDBusArgument>()) {
        return {};
    }
    const QDBusArgument argument = value.value<QDBusArgument>();
    if (argument.currentType() != QDBusArgument::ArrayType) {
        return {};
    }
    QStringList values;
    argument.beginArray();
    while (!argument.atEnd()) {
        QString text;
        argument >> text;
        if (!text.isEmpty()) {
            values.append(text);
        }
    }
    argument.endArray();
    return values;
}

bool relevantProperty(const QVariantMap& changed, const QStringList& invalidated) {
    const QStringList keys = {
        QStringLiteral("PrimaryConnection"),
        QStringLiteral("ActiveConnections"),
        QStringLiteral("State"),
    };
    for (const QString& key : keys) {
        if (changed.contains(key) || invalidated.contains(key)) {
            return true;
        }
    }
    return false;
}

} // namespace

NetworkManagerWatcher::NetworkManagerWatcher(QObject* parent) : QObject(parent) {
    m_timer.setSingleShot(true);
    m_timer.setInterval(kRefreshDelayMs);
    connect(&m_timer, &QTimer::timeout, this, &NetworkManagerWatcher::refresh);
}

int NetworkManagerWatcher::ifindex() const {
    return m_ifindex;
}

QString NetworkManagerWatcher::interfaceName() const {
    return m_name;
}

QString NetworkManagerWatcher::lastError() const {
    return m_error;
}

LinkDnsConfiguration NetworkManagerWatcher::linkDns(int ifindex) const {
    LinkDnsConfiguration config;
    if (ifindex <= 0) {
        return config;
    }

    QString propertyError;
    const QStringList devices =
        objectPaths(readProperty(QStringLiteral("/org/freedesktop/NetworkManager"),
                                 QStringLiteral("org.freedesktop.NetworkManager"),
                                 QStringLiteral("Devices"), &propertyError));
    if (devices.isEmpty()) {
        config.error = propertyError.isEmpty() ? QStringLiteral("NetworkManager has no devices.")
                                               : propertyError;
        return config;
    }

    QString devicePath;
    const QString deviceInterface = QStringLiteral("org.freedesktop.NetworkManager.Device");
    for (const QString& path : devices) {
        QString deviceError;
        QString interfaceName =
            readProperty(path, deviceInterface, QStringLiteral("IpInterface"), &deviceError)
                .toString();
        if (interfaceName.isEmpty()) {
            interfaceName =
                readProperty(path, deviceInterface, QStringLiteral("Interface"), &deviceError)
                    .toString();
        }
        if (interfaceName.isEmpty()) {
            continue;
        }
        if (static_cast<int>(if_nametoindex(interfaceName.toUtf8().constData())) == ifindex) {
            devicePath = path;
            break;
        }
    }
    if (devicePath.isEmpty()) {
        return config;
    }

    const auto appendConfig = [&](const QString& path, const QString& interface, bool ipv4) {
        if (path.isEmpty() || path == QLatin1String("/")) {
            return;
        }
        QString ignored;
        QList<DnsServerEndpoint> servers = serversFromNameserverData(
            readProperty(path, interface, QStringLiteral("NameserverData"), &ignored));
        if (servers.isEmpty() && ipv4) {
            servers = serversFromIpv4Nameservers(
                readProperty(path, interface, QStringLiteral("Nameservers"), &ignored));
        }
        const int priority =
            readProperty(path, interface, QStringLiteral("DnsPriority"), &ignored).toInt();
        if (!servers.isEmpty() && priority >= 0) {
            config.defaultRoute = true;
        }
        config.servers.append(servers);
        for (const QString& search : stringListFromValue(
                 readProperty(path, interface, QStringLiteral("Searches"), &ignored))) {
            if (!config.searchDomains.contains(search)) {
                config.searchDomains.append(search);
            }
        }
    };

    QString configError;
    const QString ip4 = objectPath(
        readProperty(devicePath, deviceInterface, QStringLiteral("Ip4Config"), &configError));
    const QString ip6 = objectPath(
        readProperty(devicePath, deviceInterface, QStringLiteral("Ip6Config"), &configError));
    appendConfig(ip4, QStringLiteral("org.freedesktop.NetworkManager.IP4Config"), true);
    appendConfig(ip6, QStringLiteral("org.freedesktop.NetworkManager.IP6Config"), false);
    return config;
}

void NetworkManagerWatcher::start() {
    QDBusConnection bus = QDBusConnection::systemBus();
    bus.connect(QStringLiteral("org.freedesktop.NetworkManager"),
                QStringLiteral("/org/freedesktop/NetworkManager"),
                QStringLiteral("org.freedesktop.DBus.Properties"),
                QStringLiteral("PropertiesChanged"), this,
                SLOT(onPropertiesChanged(QString, QVariantMap, QStringList)));
    bus.connect(QStringLiteral("org.freedesktop.NetworkManager"),
                QStringLiteral("/org/freedesktop/NetworkManager"),
                QStringLiteral("org.freedesktop.NetworkManager"),
                QStringLiteral("PropertiesChanged"), this,
                SLOT(onLegacyPropertiesChanged(QVariantMap)));
    bus.connect(QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
                QStringLiteral("org.freedesktop.DBus"), QStringLiteral("NameOwnerChanged"), this,
                SLOT(onNameOwnerChanged(QString, QString, QString)));
    refresh();
    m_notify = true;
}

void NetworkManagerWatcher::onPropertiesChanged(const QString& interface,
                                                const QVariantMap& changed,
                                                const QStringList& invalidated) {
    if (interface != QLatin1String("org.freedesktop.NetworkManager")) {
        return;
    }
    if (relevantProperty(changed, invalidated)) {
        schedule();
    }
}

void NetworkManagerWatcher::onLegacyPropertiesChanged(const QVariantMap& changed) {
    if (relevantProperty(changed, {})) {
        schedule();
    }
}

void NetworkManagerWatcher::onActiveStateChanged(uint state, uint reason) {
    Q_UNUSED(reason);
    m_activationPending = state == kConnectionActivated;
    schedule();
}

void NetworkManagerWatcher::onNameOwnerChanged(const QString& name, const QString& oldOwner,
                                               const QString& newOwner) {
    Q_UNUSED(oldOwner);
    Q_UNUSED(newOwner);
    if (name == QLatin1String("org.freedesktop.NetworkManager")) {
        schedule();
    }
}

void NetworkManagerWatcher::schedule() {
    if (!m_timer.isActive()) {
        m_timer.start();
    }
}

void NetworkManagerWatcher::subscribePrimary(const QString& path) {
    if (path == m_subscribedPath) {
        return;
    }
    QDBusConnection bus = QDBusConnection::systemBus();
    if (!m_subscribedPath.isEmpty() && m_subscribedPath != QLatin1String("/")) {
        bus.disconnect(QStringLiteral("org.freedesktop.NetworkManager"), m_subscribedPath,
                       QStringLiteral("org.freedesktop.NetworkManager.Connection.Active"),
                       QStringLiteral("StateChanged"), this,
                       SLOT(onActiveStateChanged(uint, uint)));
    }
    m_subscribedPath = path;
    if (path.isEmpty() || path == QLatin1String("/")) {
        return;
    }
    bus.connect(QStringLiteral("org.freedesktop.NetworkManager"), path,
                QStringLiteral("org.freedesktop.NetworkManager.Connection.Active"),
                QStringLiteral("StateChanged"), this, SLOT(onActiveStateChanged(uint, uint)));
}

bool NetworkManagerWatcher::readLink(int* ifindex, QString* name, QString* primaryPath,
                                     QString* error) const {
    *ifindex = 0;
    name->clear();
    primaryPath->clear();
    error->clear();

    const QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.isConnected()) {
        *error = QStringLiteral("System D-Bus is not available.");
        return false;
    }
    const QDBusReply<bool> registered =
        bus.interface()->isServiceRegistered(QStringLiteral("org.freedesktop.NetworkManager"));
    if (!registered.isValid() || !registered.value()) {
        *error = QStringLiteral("NetworkManager is not running.");
        return false;
    }

    QString propertyError;
    const QVariant primaryValue = readProperty(QStringLiteral("/org/freedesktop/NetworkManager"),
                                               QStringLiteral("org.freedesktop.NetworkManager"),
                                               QStringLiteral("PrimaryConnection"), &propertyError);
    const QString primary = objectPath(primaryValue);
    if (primary.isEmpty() || primary == QLatin1String("/")) {
        return true;
    }
    *primaryPath = primary;

    const QVariant devicesValue =
        readProperty(primary, QStringLiteral("org.freedesktop.NetworkManager.Connection.Active"),
                     QStringLiteral("Devices"), &propertyError);
    const QStringList devices = objectPaths(devicesValue);
    if (devices.isEmpty()) {
        *error = propertyError.isEmpty() ? QStringLiteral("The primary connection has no device.")
                                         : propertyError;
        return false;
    }

    int bestIndex = 0;
    QString bestName;
    bool bestActivated = false;
    bool found = false;
    for (const QString& devicePath : devices) {
        QString deviceError;
        const uint state =
            readProperty(devicePath, QStringLiteral("org.freedesktop.NetworkManager.Device"),
                         QStringLiteral("State"), &deviceError)
                .toUInt();
        QString interfaceName =
            readProperty(devicePath, QStringLiteral("org.freedesktop.NetworkManager.Device"),
                         QStringLiteral("IpInterface"), &deviceError)
                .toString();
        if (interfaceName.isEmpty()) {
            interfaceName =
                readProperty(devicePath, QStringLiteral("org.freedesktop.NetworkManager.Device"),
                             QStringLiteral("Interface"), &deviceError)
                    .toString();
        }
        if (interfaceName.isEmpty()) {
            continue;
        }
        const unsigned index = if_nametoindex(interfaceName.toUtf8().constData());
        if (index == 0) {
            continue;
        }
        const bool activated = state == kDeviceActivated;
        if (!found || (activated && !bestActivated)) {
            found = true;
            bestActivated = activated;
            bestIndex = static_cast<int>(index);
            bestName = interfaceName;
        }
    }
    if (!found) {
        *error = QStringLiteral("The primary connection has no interface index.");
        return false;
    }
    *ifindex = bestIndex;
    *name = bestName;
    return true;
}

void NetworkManagerWatcher::refresh() {
    int index = 0;
    QString name;
    QString primary;
    QString error;
    readLink(&index, &name, &primary, &error);
    subscribePrimary(primary);

    const bool structural = index != m_ifindex || name != m_name;
    // A replacement may already be activated before we subscribe to its signals.
    const bool activation = m_activationPending || (primary != m_primaryPath && index > 0);
    m_ifindex = index;
    m_name = name;
    m_error = error;
    m_primaryPath = primary;
    m_activationPending = false;
    if (!m_notify) {
        return;
    }
    if (activation) {
        emit primaryLinkActivated();
        return;
    }
    if (structural) {
        emit primaryLinkChanged();
    }
}

} // namespace controld
