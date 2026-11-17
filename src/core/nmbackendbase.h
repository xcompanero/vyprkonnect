#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <functional>

#include "servermodel.h"

namespace vypr {

// Shared NetworkManager-driven state machine for both OpenVPN and WireGuard
// backends. Encodes the cross-protocol rules:
//  - conflict detection before connecting (Vypr blocks nested tunnels)
//  - stateless disconnect (scan for our own active profile)
//  - honest status surfaces (stateLabel single source of truth)
class NmBackendBase : public QObject
{
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool canConnect READ canConnect NOTIFY stateChanged)
    Q_PROPERTY(bool canDisconnect READ canDisconnect NOTIFY stateChanged)
    Q_PROPERTY(QString stateLabel READ stateLabel NOTIFY stateChanged)
    Q_PROPERTY(QString activeServer READ activeServer NOTIFY stateChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
public:
    enum State { Disconnected, Connecting, Connected, Disconnecting, Failed };
    Q_ENUM(State)

    explicit NmBackendBase(ServerModel *servers, QObject *parent = nullptr);

    virtual QString backendName() const = 0;
    Q_INVOKABLE virtual void connectToServer(const QString &serverName) = 0;
    Q_INVOKABLE virtual void disconnect() = 0;
    Q_INVOKABLE QString wanIpAddress() const { return {}; }

    // Reflect an already-established tunnel (found at startup in NM) so the UI
    // shows reality instead of a blank "Not Connected" state.
    void recoverActiveProfile(const QString &profileIdStr, const QString &serverName)
    {
        m_activeServer = profileIdStr;
        m_activeServerName = serverName;
        setState(Connected);
        setMessage(QString());
        startStatePolling();
    }

    State state() const { return m_state; }
    QString activeServer() const { return m_activeServer; }
    QString statusMessage() const { return m_statusMessage; }

    bool connected() const { return m_state == Connected; }
    bool busy() const { return m_state == Connecting || m_state == Disconnecting; }
    bool canConnect() const { return m_state == Disconnected || m_state == Failed; }
    bool canDisconnect() const { return m_state == Connected || m_state == Disconnecting; }
    QString stateLabel() const
    {
        if (!m_statusMessage.isEmpty())
            return m_statusMessage;
        switch (m_state) {
        case Connected: return m_activeServerName.isEmpty()
            ? QObject::tr("Connected")
            : QObject::tr("Connected to %1").arg(m_activeServerName);
        case Connecting: return QObject::tr("Connecting…");
        case Disconnecting: return QObject::tr("Disconnecting…");
        case Failed: return QObject::tr("Connection failed");
        case Disconnected: break;
        }
        return QObject::tr("Not Connected");
    }

signals:
    void stateChanged();
    void statusMessageChanged();

protected:
    using Continuation = std::function<void(int exitCode, const QString &output)>;

    // Common connect preamble: validate server + credentials, then resolve any
    // active-VPN conflict, then run `proceed`.
    void beginConnect(const QString &serverName, const std::function<void(const vypr::Server &)> &proceed);

    void runNmcli(const QStringList &args, Continuation cont, int timeoutMs = 45000);
    void setState(State s);
    void setMessage(const QString &msg);
    void checkActiveVpnThen(const std::function<void(bool conflict, const QString &activeName)> &next);
    void resolveConflictThen(const std::function<void()> &proceed);
    void startStatePolling();
    void stopStatePolling();
    QString profileId(const vypr::Server &server) const;

    ServerModel *m_servers = nullptr;
    State m_state = Disconnected;
    QString m_activeServer;
    QString m_activeServerName;
    QString m_statusMessage;
    QString m_pendingServer;
    QTimer m_pollTimer;
};

} // namespace vypr
