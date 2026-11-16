#pragma once

#include <QAbstractListModel>
#include <QVector>

namespace vypr {

struct Server {
    QString name;
    QString hostname;
    quint16 port = 443;
    QString proto = QStringLiteral("udp");
};

// Read-only list model of VPN server locations, loaded from servers.json.
class ServerModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    enum Roles { NameRole = Qt::UserRole + 1, HostnameRole, PortRole };

    explicit ServerModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_servers.size()); }
    Q_INVOKABLE vypr::Server get(int index) const;
    Q_INVOKABLE int indexOf(const QString &serverName) const;
    bool loadFromFile(const QString &path);

signals:
    void countChanged();

private:
    QVector<Server> m_servers;
};

} // namespace vypr
