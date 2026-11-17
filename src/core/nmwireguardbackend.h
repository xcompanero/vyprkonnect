#pragma once

#include "nmbackendbase.h"

#include <QTemporaryDir>

namespace vypr {

// WireGuard over NetworkManager, provisioned through Vypr's official
// `vyprvpn-wireguard-go` CLI (Route A):
//   1. provision: CLI fetches a peer config from the Vypr WG API (no root needed;
//      the CLI's own wg-quick step is never used — it fails harmlessly after the
//      config file is written, or is killed once the file appears)
//   2. import: the parsed config becomes a NetworkManager wireguard connection
//   3. disconnect: NM down + CLI `disconnect` releases the server-side session
//      via the VyprVPN-Connection-ID recorded in the config file
class NmWireGuardBackend : public NmBackendBase
{
    Q_OBJECT
public:
    explicit NmWireGuardBackend(ServerModel *servers, QObject *parent = nullptr);

    QString backendName() const override { return QStringLiteral("WireGuard"); }
    void connectToServer(const QString &serverName) override;
    void disconnect() override;

    // Where the official CLI binary is expected; overridable for dev/testing.
    static QString findCli();

private:
    struct WgConfig {
        QString interfaceAddress;   // 10.4.x.x/32
        QString dns;                // 10.4.0.1
        QString listenPort;
        QString privateKey;         // secret
        QString peerPublicKey;
        QString presharedKey;       // secret
        QString endpoint;           // host:port
        QString allowedIps;
        QString connectionId;       // VyprVPN-Connection-ID comment
        QString wgUrl;              // VyprVPN-WgUrl comment
        bool valid = false;
    };

    void provisionAndImport(const vypr::Server &server);
    void runCli(const QStringList &args, const std::function<void(int, const QString &)> &cont,
                int timeoutMs = 30000);
    void ensureCliConfigFile(const std::function<void(const QString &path, const QString &wgConfPath)> &next);
    static WgConfig parseWgConfig(const QString &text);
    void createNmProfile(const vypr::Server &server, const WgConfig &cfg,
                         const QString &confPath, QTemporaryDir *tmpDir);
    void addConnection(const vypr::Server &server, const WgConfig &cfg,
                       const QString &confPath, QTemporaryDir *tmpDir);
    // Writes/removes the endpoint host route on the PHYSICAL uplink profile
    // (pre-routed before the tunnel activates, so the encapsulation loop can
    // never start). Best-effort; the NM dispatcher hook is the safety net.
    void configureEndpointRoute(const QString &endpointIp, bool add,
                                const std::function<void()> &next = {});
    QString m_endpointIp;
    void releaseWireGuardSession(const std::function<void()> &done);
    // NM declares WireGuard "activated" before any handshake occurs; verify
    // real traffic through the tunnel before reporting Connected.
    void verifyLiveness();
    int m_verifyAttempts = 0;
};

} // namespace vypr
