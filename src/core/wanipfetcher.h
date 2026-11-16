#pragma once

#include <QObject>
#include <QString>
#include <QNetworkAccessManager>

namespace vypr {

// Fetches the current WAN (egress) IP from ifconfig.io.
// Refreshed automatically when the connection state changes.
class WanIpFetcher : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString address READ address NOTIFY addressChanged)
public:
    explicit WanIpFetcher(QObject *parent = nullptr);

    QString address() const { return m_address; }
    Q_INVOKABLE void refresh();

signals:
    void addressChanged();

private:
    QNetworkAccessManager m_nam;
    QString m_address;
};

} // namespace vypr
