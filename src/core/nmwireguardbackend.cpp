#include "nmwireguardbackend.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QHostAddress>
#include <QStandardPaths>
#include <QTimer>

#include "credentialstore.h"

namespace vypr {

using namespace Qt::Literals::StringLiterals;

NmWireGuardBackend::NmWireGuardBackend(ServerModel *servers, QObject *parent)
    : NmBackendBase(servers, parent)
{
}

QString NmWireGuardBackend::findCli()
{
    // 1. Explicit override (dev/testing), 2. installed in PATH per Vypr's setup guide.
    const QString env = qEnvironmentVariable("VYPRKONNECT_WG_CLI");
    if (!env.isEmpty())
        return env;
    return QStandardPaths::findExecutable(u"vyprvpn-wireguard-go"_s);
}

void NmWireGuardBackend::connectToServer(const QString &serverName)
{
    beginConnect(serverName, [this](const vypr::Server &server) {
        provisionAndImport(server);
    });
}

void NmWireGuardBackend::ensureCliConfigFile(
    const std::function<void(const QString &, const QString &)> &next)
{
    // The official CLI reads credentials from ~/.config/vyprvpn/config.yaml
    // (its own documented design). We materialise it from our credential store.
    const QString base = qEnvironmentVariable("XDG_CONFIG_HOME").isEmpty()
        ? QDir::homePath() + u"/.config"_s
        : qEnvironmentVariable("XDG_CONFIG_HOME");
    QDir().mkpath(base + u"/vyprvpn"_s);
    const QString cfgPath = base + u"/vyprvpn/config.yaml"_s;
    QFile f(cfgPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setMessage(u"Cannot write the WireGuard CLI configuration (%1)."_s.arg(cfgPath));
        setState(Failed);
        return;
    }
    const QString user = CredentialStore::instance().username();
    const QString pass = CredentialStore::instance().password();
    f.write((u"account:\n  username: %1\n  password: %2\n"_s.arg(user, pass)).toUtf8());
    f.close();
    f.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    next(cfgPath, base + u"/vyprvpn/wg-vyprkonnect.conf"_s);
}

void NmWireGuardBackend::runCli(const QStringList &args,
                                const std::function<void(int, const QString &)> &cont,
                                int timeoutMs)
{
    const QString cli = findCli();
    if (cli.isEmpty()) {
        setMessage(u"The VyprVPN WireGuard CLI (vyprvpn-wireguard-go) was not found. "
                   "Install it per Vypr's setup guide, or set VYPRKONNECT_WG_CLI."_s);
        setState(Failed);
        return;
    }
    auto *proc = new QProcess(this);
    proc->setProgram(cli);
    proc->setArguments(args);
    connect(proc, &QProcess::finished, this, [proc, cont](int code, QProcess::ExitStatus st) {
        qDebug("wg-cli %s -> exit=%d", qPrintable(proc->arguments().join(u' ')), code);
        cont(st == QProcess::NormalExit ? code : -1,
             QString::fromUtf8(proc->readAllStandardOutput()));
        proc->deleteLater();
    });
    QTimer::singleShot(timeoutMs, proc, [proc] {
        if (proc->state() != QProcess::NotRunning)
            proc->kill();
    });
    proc->start();
}

NmWireGuardBackend::WgConfig NmWireGuardBackend::parseWgConfig(const QString &text)
{
    WgConfig cfg;
    QString section;
    for (const QString &raw : text.split(u'\n')) {
        const QString line = raw.trimmed();
        if (line.startsWith(u'#')) {
            if (line.startsWith(u"# VyprVPN-Connection-ID = "_s))
                cfg.connectionId = line.mid(QString(u"# VyprVPN-Connection-ID = "_s).size()).trimmed();
            else if (line.startsWith(u"# VyprVPN-WgUrl = "_s))
                cfg.wgUrl = line.mid(QString(u"# VyprVPN-WgUrl = "_s).size()).trimmed();
            continue;
        }
        if (line.startsWith(u'[')) {
            section = line;
            continue;
        }
        const int eq = line.indexOf(u'=');
        if (eq < 0)
            continue;
        const QString key = line.left(eq).trimmed();
        const QString val = line.mid(eq + 1).trimmed();
        if (section.compare(u"[Interface]"_s, Qt::CaseInsensitive) == 0) {
            if (key == u"Address"_s) cfg.interfaceAddress = val;
            else if (key == u"DNS"_s) cfg.dns = val;
            else if (key == u"ListenPort"_s) cfg.listenPort = val;
            else if (key == u"PrivateKey"_s) cfg.privateKey = val;
        } else if (section.compare(u"[Peer]"_s, Qt::CaseInsensitive) == 0) {
            if (key == u"PublicKey"_s) cfg.peerPublicKey = val;
            else if (key == u"PresharedKey"_s) cfg.presharedKey = val;
            else if (key == u"Endpoint"_s) cfg.endpoint = val;
            else if (key == u"AllowedIPs"_s) cfg.allowedIps = val;
        }
    }
    cfg.valid = !cfg.privateKey.isEmpty() && !cfg.peerPublicKey.isEmpty()
        && !cfg.endpoint.isEmpty() && !cfg.interfaceAddress.isEmpty();
    return cfg;
}

void NmWireGuardBackend::provisionAndImport(const vypr::Server &server)
{
    const QString id = profileId(server);
    ensureCliConfigFile([this, server, id](const QString &cfgPath, const QString &wgConfPath) {
        // Provision via the official CLI. It writes the config then tries
        // wg-quick (which either fails fast without root or waits on a
        // handshake that never happens) — so we stop as soon as the file
        // exists; the tunnel itself is NetworkManager's job, not wg-quick's.
        auto *proc = new QProcess(this);
        proc->setProgram(findCli());
        proc->setArguments({u"connect"_s, u"-server"_s, server.name,
                            u"-config"_s, cfgPath,
                            u"-wgfilename"_s, wgConfPath});
        QTimer::singleShot(25000, proc, [proc] {
            if (proc->state() != QProcess::NotRunning)
                proc->kill();
        });
        connect(proc, &QProcess::finished, this, [this, server, id, wgConfPath, proc]
                (int code, QProcess::ExitStatus) {
            proc->deleteLater();
            qDebug("wg-cli connect -> exit=%d", code);

            QFile conf(wgConfPath);
            if (!conf.open(QIODevice::ReadOnly)) {
                setMessage(u"WireGuard provisioning failed: no configuration was produced."_s);
                setState(Failed);
                return;
            }
            const WgConfig cfg = parseWgConfig(QString::fromUtf8(conf.readAll()));
            conf.close();
            if (!cfg.valid) {
                setMessage(u"WireGuard provisioning produced an unusable configuration."_s);
                setState(Failed);
                return;
            }
            createNmProfile(server, cfg, wgConfPath, nullptr);
        });
        proc->start();
    });
}

namespace {

// Nested D-Bus types used by NM's AddConnection: register them once so Qt
// marshals them natively (QVariantMap -> a{sv}, QList<QVariantMap> -> aa{sv},
// QList<QStringList> -> aas).
void registerDBusTypes()
{
    static bool done = false;
    if (done)
        return;
    qDBusRegisterMetaType<QVariantMap>();
    qDBusRegisterMetaType<QList<QVariantMap>>();
    qDBusRegisterMetaType<QList<uint>>();
    done = true;
}

} // namespace

void NmWireGuardBackend::createNmProfile(const vypr::Server &server, const WgConfig &cfg,
                                         const QString &confPath, QTemporaryDir *tmpDir)
{
    const QString id = profileId(server);
    registerDBusTypes();

    // NM 1.54 asserts (crashes!) on AddConnection when a profile with the same
    // id already exists — remove any stale profile first. Done here, at the top
    // of profile creation, because provisioning has already succeeded by now.
    // (Delete-before-create is mandatory, not an optimisation.)
    runNmcli({u"connection"_s, u"delete"_s, id}, [this, server, cfg, confPath, tmpDir](int, const QString &) {
        addConnection(server, cfg, confPath, tmpDir);
    });
}

void NmWireGuardBackend::addConnection(const vypr::Server &server, const WgConfig &cfg,
                                       const QString &confPath, QTemporaryDir *tmpDir)
{
    const QString id = profileId(server);
    registerDBusTypes();

    // nmcli 1.54 cannot script wireguard peers, so the profile is created via
    // the NetworkManager D-Bus API (AddConnection), where the peers structure
    // is fully expressible. The secrets stay in NM's root-owned profile store.

    QVariantMap peer;
    peer.insert(u"public-key"_s, cfg.peerPublicKey);
    peer.insert(u"preshared-key"_s, cfg.presharedKey);
    peer.insert(u"preshared-key-flags"_s, uint(0));
    peer.insert(u"endpoint"_s, cfg.endpoint);
    // NM expects allowed-ips as plain 'as' (array of CIDR strings).
    peer.insert(u"allowed-ips"_s, cfg.allowedIps.split(u',', Qt::SkipEmptyParts));
    peer.insert(u"persistent-keepalive"_s, uint(25));

    QVariantMap wg;
    wg.insert(u"private-key"_s, cfg.privateKey);
    wg.insert(u"private-key-flags"_s, uint(0));
    // Always let the kernel pick the listen port: the provisioning port may
    // conflict with other services, and a random port is equally valid.
    wg.insert(u"listen-port"_s, uint(0));
    wg.insert(u"peer-routes"_s, true);
    wg.insert(u"ip4-auto-default-route"_s, true);
    wg.insert(u"peers"_s, QVariant::fromValue(QList<QVariantMap>{ peer }));

    QVariantMap ipv4;
    ipv4.insert(u"method"_s, u"manual"_s);
    QVariantMap addr;
    addr.insert(u"address"_s, cfg.interfaceAddress.section(u'/', 0, 0));
    addr.insert(u"prefix"_s, cfg.interfaceAddress.section(u'/', 1).toUInt());
    ipv4.insert(u"address-data"_s, QVariant::fromValue(QList<QVariantMap>{ addr }));
    // NOTE: tunnel DNS (VyprDNS 10.4.0.1) is deliberately NOT configured.
    // Field testing (16 Nov 2026) showed systemd-resolved wedges when the WG
    // link's DNS competes with Tailscale's MagicDNS as a global resolver —
    // every lookup on the machine times out and the desktop freezes. This
    // mirrors the official CLI's `-ignoredns` behaviour. A "Use VyprDNS"
    // setting can be offered later for machines without such conflicts.

    QVariantMap connection;
    connection.insert(u"id"_s, id);
    connection.insert(u"type"_s, u"wireguard"_s);
    connection.insert(u"interface-name"_s, u"vyprwg0"_s);
    connection.insert(u"autoconnect"_s, false);

    // Outer settings map a{sa{sv}}: QMap<QString, QVariantMap> marshals as
    // exactly that — no QDBusArgument gymnastics needed (and no double-variant
    // crashes).
    QMap<QString, QVariantMap> settings;
    settings.insert(u"connection"_s, connection);
    settings.insert(u"wireguard"_s, wg);
    settings.insert(u"ipv4"_s, ipv4);


    QDBusMessage msg = QDBusMessage::createMethodCall(
        u"org.freedesktop.NetworkManager"_s,
        u"/org/freedesktop/NetworkManager/Settings"_s,
        u"org.freedesktop.NetworkManager.Settings"_s,
        u"AddConnection"_s);
    msg << QVariant::fromValue(settings);

    QDBusMessage reply = QDBusConnection::systemBus().call(msg);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        setMessage(u"Failed to create WireGuard profile: %1 (%2)"_s
                       .arg(reply.errorName(), reply.errorMessage()));
        setState(Failed);
        return;
    }

    m_activeServer = id;
    m_activeServerName = server.name;
    emit stateChanged();
    runNmcli({u"connection"_s, u"up"_s, id},
             [this, confPath, tmpDir](int upcode, const QString &upout) {
                 if (upcode == 0) {
                     setState(Connected);
                     setMessage(QString());
                     startStatePolling();
                 } else {
                     m_activeServer.clear();
                     m_activeServerName.clear();
                     setMessage(u"Connection attempt failed: %1"_s.arg(upout.trimmed()));
                     setState(Failed);
                 }
             });
}

void NmWireGuardBackend::disconnect()
{
    if (m_state == Disconnecting)
        return;
    setState(Disconnecting);
    stopStatePolling();
    checkActiveVpnThen([this](bool conflict, const QString &activeName) {
        if (!conflict || !activeName.startsWith(u"VyprKonnect")) {
            m_activeServer.clear();
            m_activeServerName.clear();
            setState(Disconnected);
            emit stateChanged();
            return;
        }
        runNmcli({u"connection"_s, u"down"_s, activeName}, [this, activeName](int, const QString &) {
            if (m_activeServer == activeName) {
                m_activeServer.clear();
                m_activeServerName.clear();
            }
            // Release the server-side session via the official CLI. The conf
            // file carries the VyprVPN-Connection-ID the CLI needs. Best-effort:
            // the local tunnel is already down regardless of the outcome.
            const QString confPath = QDir::homePath()
                + u"/.config/vyprvpn/wg-vyprkonnect.conf"_s;
            ensureCliConfigFile([this](const QString &cfgPath, const QString &wgConfPath) {
                runCli({u"disconnect"_s, u"-config"_s, cfgPath, u"-wgfilename"_s, wgConfPath},
                       [this](int, const QString &) {
                           setState(Disconnected);
                           emit stateChanged();
                       });
            });
        });
    });
}

} // namespace vypr
