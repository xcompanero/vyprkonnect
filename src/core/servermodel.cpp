#include "servermodel.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

namespace vypr {

ServerModel::ServerModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int ServerModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_servers.size());
}

QVariant ServerModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= int(m_servers.size()))
        return {};
    const Server &s = m_servers[index.row()];
    switch (role) {
    case NameRole: return s.name;
    case HostnameRole: return s.hostname;
    case PortRole: return s.port;
    }
    return {};
}

QHash<int, QByteArray> ServerModel::roleNames() const
{
    return {{NameRole, "name"}, {HostnameRole, "hostname"}, {PortRole, "port"}};
}

Server ServerModel::get(int index) const
{
    return (index >= 0 && index < int(m_servers.size())) ? m_servers[index] : Server{};
}

int ServerModel::indexOf(const QString &serverName) const
{
    for (int i = 0; i < int(m_servers.size()); ++i)
        if (m_servers[i].name == serverName)
            return i;
    return -1;
}

bool ServerModel::loadFromFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    const QJsonArray arr = doc.object()[QStringLiteral("servers")].toArray();
    beginResetModel();
    m_servers.clear();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        m_servers.append({o[QStringLiteral("name")].toString(),
                          o[QStringLiteral("hostname")].toString(),
                          quint16(o[QStringLiteral("port")].toInt(443)),
                          o[QStringLiteral("proto")].toString(QStringLiteral("udp"))});
    }
    endResetModel();
    emit countChanged();
    return !m_servers.isEmpty();
}

} // namespace vypr
