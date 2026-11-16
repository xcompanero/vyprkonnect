#pragma once

#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QTimer>

#include "servermodel.h"

namespace vypr {

// Manages VyprVPN OpenVPN connections through NetworkManager (nmcli).
//
// Encodes the Phase 0 field-test rules:
//  1. Rewrite `compress stub-v2` -> `compress` before import (NM plugin rejects stub-v2).
//  2. Set username explicitly after import (import leaves it empty).
//  3. Use compress=asymmetric per Vypr's Ubuntu KB / "Asymmetric and Automatic".
//  4. Detect an already-active VPN and handle conflicts explicitly (Vypr blocks
//     nested tunnels — a new connect through an existing tunnel fails TLS).
//  5. Feed credentials at activation via passwd-file (0600, deleted immediately).
//     Works for both agent-owned and NM-owned secret flags.
class NmOpenVpnBackend : public QObject
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

    explicit NmOpenVpnBackend(ServerModel *servers, QObject *parent = nullptr);

    State state() const { return m_state; }
    QString activeServer() const { return m_activeServer; }
    QString statusMessage() const { return m_statusMessage; }

    bool connected() const { return m_state == Connected; }
    bool busy() const { return m_state == Connecting || m_state == Disconnecting; }
    bool canConnect() const { return m_state == Disconnected || m_state == Failed; }
    bool canDisconnect() const { return m_state == Connected || m_state == Disconnecting; }
    // Human-readable, single source of truth for the UI status line.
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

    Q_INVOKABLE void connectToServer(const QString &serverName);
    Q_INVOKABLE void disconnect();
    Q_INVOKABLE QString wanIpAddress() const;

signals:
    void stateChanged();
    void statusMessageChanged();

private:
    using Continuation = std::function<void(int exitCode, const QString &output)>;

    void runNmcli(const QStringList &args, Continuation cont, int timeoutMs = 45000);
    void setState(State s);
    void setMessage(const QString &msg);
    void checkActiveVpnThen(const std::function<void(bool conflict, const QString &activeName)> &next);
    void ensureProfileAndConnect(const vypr::Server &server);
    void finishProfileSetup(const QString &id, int code, const QString &out,
                            QTemporaryDir *tmpDir);
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
