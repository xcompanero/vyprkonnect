#include "nmwireguardbackend.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QHostAddress>
#include <QtEndian>
#include <QStandardPaths>
#include <QTimer>

#include "credentialstore.h"

namespace vypr {

using namespace Qt::Literals::StringLiterals;

NmWireGuardBackend::NmWireGuardBackend(ServerModel *servers, QObject *parent)
    : NmBackendBase(servers, parent)
{
    connect(&m_sessionWatch, &QTimer::timeout, this, &NmWireGuardBackend::probeSession);
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


// Best-effort: write/remove "EP/32 via GW" in the physical uplink profile.
namespace {

// Physical default-route device/gateway at this moment (pre-activation, so
// the main table's default is the real uplink).
struct PhysicalRoute {
    QString device;
    QString gateway; // may be empty for on-link
};

PhysicalRoute physicalDefaultRoute()
{
    PhysicalRoute r;
    QProcess p;
    p.start(u"ip"_s, {u"route"_s, u"show"_s, u"default"_s});
    p.waitForFinished(3000);
    const QString out = QString::fromUtf8(p.readAllStandardOutput());
    const QString line = out.section(u'\n', 0, 0);
    static const QRegularExpression re(u"via ([0-9.]+) dev ([a-z0-9]+)"_s);
    const QRegularExpressionMatch m = re.match(line);
    if (m.hasMatch()) {
        r.gateway = m.captured(1);
        r.device = m.captured(2);
    }
    return r;
}

QString activeProfileForDevice(const QString &device)
{
    QProcess p;
    p.start(u"nmcli"_s, {u"-t"_s, u"-f"_s, u"NAME,DEVICE,STATE"_s,
                          u"connection"_s, u"show"_s, u"--active"_s});
    p.waitForFinished(3000);
    for (const QString &line : QString::fromUtf8(p.readAllStandardOutput())
             .split(u'\n', Qt::SkipEmptyParts)) {
        // Fields are NAME:DEVICE:STATE; NAME may contain colons.
        const int lastColon = line.lastIndexOf(u':');
        const int prevColon = line.lastIndexOf(u':', lastColon - 1);
        if (prevColon < 0)
            continue;
        const QString dev = line.mid(prevColon + 1, lastColon - prevColon - 1);
        if (dev == device)
            return line.left(prevColon);
    }
    return {};
}

QString lastEndpointPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + u"/wg-last-endpoint"_s;
}

} // namespace

void NmWireGuardBackend::configureEndpointRoute(const QString &endpointIp, bool add,
                                                const std::function<void()> &next)
{
    const PhysicalRoute phy = physicalDefaultRoute();
    if (phy.device.isEmpty()) {
        if (next) next();
        return;
    }
    const QString profile = activeProfileForDevice(phy.device);
    if (profile.isEmpty()) {
        if (next) next();
        return;
    }
    const QString spec = phy.gateway.isEmpty()
        ? u"%1/32"_s.arg(endpointIp)
        : u"%1/32 %2"_s.arg(endpointIp, phy.gateway);
    const QString ipSpec = u"%1/32"_s.arg(endpointIp);

    // Clean up a stale endpoint route first (best-effort, ignore result).
    QFile state(lastEndpointPath());
    QString old;
    if (state.open(QIODevice::ReadOnly)) {
        old = QString::fromUtf8(state.readAll()).trimmed();
        state.close();
    }
    if (!old.isEmpty() && old != spec) {
        QProcess p;
        p.start(u"nmcli"_s, {u"connection"_s, u"modify"_s, profile,
                              u"-ipv4.routes"_s, old});
        p.waitForFinished(5000);
    }
    if (!add) {
        if (!spec.isEmpty()) {
            QProcess p;
            p.start(u"nmcli"_s, {u"connection"_s, u"modify"_s, profile,
                                  u"-ipv4.routes"_s, spec});
            p.waitForFinished(5000);
        }
        QFile::remove(lastEndpointPath());
        if (next) next();
        return;
    }

    runNmcli({u"connection"_s, u"modify"_s, profile,
              u"+ipv4.routes"_s, spec},
             [this, endpointIp, next, profile, phy, spec](int code, const QString &out) {
                 if (code != 0) {
                     qWarning("Could not pre-route WG endpoint: %s", qPrintable(out.trimmed()));
                     // continue anyway: the dispatcher hook is the safety net
                     if (next) next();
                     return;
                 }
                 // CRITICAL: modifying an ACTIVE connection does not touch the
                 // running device — without reapply the kernel never gets the
                 // route and the encapsulation loop fires. Reapply pushes the
                 // new route into the live device immediately.
                 runNmcli({u"device"_s, u"reapply"_s, phy.device},
                          [this, endpointIp, next, spec](int rcode, const QString &rout) {
                              if (rcode != 0)
                                  qWarning("reapply failed: %s", qPrintable(rout.trimmed()));
                              m_endpointIp = endpointIp;
                              QDir().mkpath(QStandardPaths::writableLocation(
                                  QStandardPaths::AppLocalDataLocation));
                              QFile f(lastEndpointPath());
                              if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
                                  f.write(spec.toUtf8());
                              if (next) next();
                          });
             });
}


// NM reports WireGuard "activated" as soon as the interface exists — the
// handshake is lazy and may never complete (a dead provisioned session looks
// identical to a healthy one until traffic flows). Push a real request through
// the tunnel before trusting the state; on failure, tear down and say why.
// Watch the session while Connected: probe every 60s; two consecutive
// failures mean the server-side session expired — re-provision transparently
// (fresh session, same location) instead of leaving a black-hole tunnel.
void NmWireGuardBackend::startSessionWatch()
{
    stopSessionWatch();
    m_probeFailures = 0;
    m_sessionWatch.setInterval(60000);
    m_sessionWatch.start();
}

void NmWireGuardBackend::stopSessionWatch()
{
    m_sessionWatch.stop();
    m_probeFailures = 0;
}

void NmWireGuardBackend::probeSession()
{
    if (m_state != Connected)
        return;
    auto *nam = new QNetworkAccessManager(this);
    QNetworkRequest req{QUrl(u"https://ifconfig.io/ip"_s)};
    req.setTransferTimeout(10000);
    req.setHeader(QNetworkRequest::UserAgentHeader, u"vyprkonnect/0.1"_s);
    QNetworkReply *rep = nam->get(req);
    connect(rep, &QNetworkReply::finished, this, [this, nam, rep] {
        rep->deleteLater();
        nam->deleteLater();
        if (m_state != Connected)
            return; // disconnected/reconnecting meanwhile
        if (rep->error() == QNetworkReply::NoError) {
            m_probeFailures = 0;
            return;
        }
        if (++m_probeFailures < 2)
            return; // one miss may be transient; next tick decides
        qInfo("WireGuard session expired — re-provisioning");
        const QString server = m_activeServerName;
        stopSessionWatch();
        if (!server.isEmpty())
            connectToServer(server); // full fresh cycle: down, provision, up, verify
    });
}

void NmWireGuardBackend::verifyLiveness()
{
    setMessage(QObject::tr("Verifying tunnel…"));
    auto *nam = new QNetworkAccessManager(this);
    QNetworkRequest req{QUrl(u"https://ifconfig.io/ip"_s)};
    req.setTransferTimeout(12000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader, u"vyprkonnect/0.1"_s);
    QNetworkReply *rep = nam->get(req);
    connect(rep, &QNetworkReply::finished, this, [this, nam, rep] {
        rep->deleteLater();
        nam->deleteLater();
        if (m_state != Connecting)
            return; // user aborted or state moved on meanwhile
        if (rep->error() == QNetworkReply::NoError) {
            setState(Connected);
            setMessage(QString());
            startStatePolling();
            startSessionWatch();
            return;
        }
        if (++m_verifyAttempts < 2) {
            QTimer::singleShot(2000, this, [this] {
                if (m_state == Connecting)
                    verifyLiveness();
            });
            return;
        }
        // Dead tunnel: bring it down and explain honestly.
        setMessage(QObject::tr(
            "The tunnel came up but passed no traffic — the location may be "
            "refusing connections right now. Try another location."));
        releaseWireGuardSession([this] { setState(Failed); });
    });
}


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
    addConnectionWithRetry(server, cfg, confPath, tmpDir, 0);
}

void NmWireGuardBackend::addConnectionWithRetry(const vypr::Server &server, const WgConfig &cfg,
                                                const QString &confPath, QTemporaryDir *tmpDir,
                                                int attempt)
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
    // NM's D-Bus type for ipv4.dns is au: each server as a uint32 in
    // network byte order.
    const QHostAddress dnsAddr(cfg.dns);
    // NM wants the uint32 in NETWORK byte order (bug found live: passing the
    // host-order value made resolved see "1.0.4.10" for VyprDNS "10.4.0.1").
    ipv4.insert(u"dns"_s, QVariant::fromValue(QList<uint>{
        qToBigEndian(dnsAddr.toIPv4Address()) }));
    ipv4.insert(u"dns-priority"_s, -50);
    // VyprDNS as the tunnel's resolver, with dns-priority -50 so it wins the
    // global slot from other resolvers (e.g. Tailscale's MagicDNS, which keeps
    // handling only tailnet domains via per-domain routing). Verified live
    // (17 Nov 2026): internet names via VyprDNS, *.ts.net via MagicDNS, LAN by
    // IP — all simultaneously, with the endpoint pre-route in place. (Earlier
    // "resolved wedge" observations were the encapsulation loop, not DNS.)

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
        // NM 1.54 can ABORT (crash + systemd restart) mid-AddConnection when
        // racing profile churn — surfacing as NoReply. Give NM a moment to
        // come back, then retry once.
        if (reply.errorName() == QLatin1String("org.freedesktop.DBus.Error.NoReply")
            && attempt < 1) {
            qWarning("AddConnection NoReply (NM restart?) — retrying in 8s");
            QTimer::singleShot(8000, this, [this, server, cfg, confPath, tmpDir] {
                addConnectionWithRetry(server, cfg, confPath, tmpDir, 1);
            });
            return;
        }
        setMessage(u"Failed to create WireGuard profile: %1 (%2)"_s
                       .arg(reply.errorName(), reply.errorMessage()));
        setState(Failed);
        return;
    }


    m_activeServer = id;
    m_activeServerName = server.name;
    emit stateChanged();
    // Pre-route the endpoint on the physical uplink BEFORE activation: the
    // def1 routes would otherwise capture the tunnel's own endpoint traffic
    // (encapsulation loop). Pre-routing removes the race entirely.
    configureEndpointRoute(cfg.endpoint.section(u':', 0, 0), true, [this, id, confPath, tmpDir] {
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
    });
}

void NmWireGuardBackend::disconnect()
{
    if (m_state == Disconnecting)
        return;
    setState(Disconnecting);
    stopStatePolling();
    releaseWireGuardSession([this] {
        setState(Disconnected);
        emit stateChanged();
    });
}

// Bring down the active VyprKonnect WG profile (if any), then release the
// server-side session via the official CLI, then clean the endpoint route.
// All steps best-effort: the local tunnel must come down regardless.
void NmWireGuardBackend::releaseWireGuardSession(const std::function<void()> &done)
{
    checkActiveVpnThen([this, done](bool conflict, const QString &activeName) {
        const auto afterDown = [this, done, activeName]() {
            if (!activeName.isEmpty() && m_activeServer == activeName) {
                m_activeServer.clear();
                m_activeServerName.clear();
            }
            // Release the server-side session via the official CLI (the conf
            // file carries the VyprVPN-Connection-ID the CLI needs).
            const QString confPath = QDir::homePath()
                + u"/.config/vyprvpn/wg-vyprkonnect.conf"_s;
            if (!QFile::exists(confPath)) {
                configureEndpointRoute(m_endpointIp, false, [this, done] {
                    m_endpointIp.clear();
                    done();
                });
                return;
            }
            ensureCliConfigFile([this, done](const QString &cfgPath, const QString &wgConfPath) {
                runCli({u"disconnect"_s, u"-config"_s, cfgPath, u"-wgfilename"_s, wgConfPath},
                       [this, done](int, const QString &) {
                           configureEndpointRoute(m_endpointIp, false, [this, done] {
                               m_endpointIp.clear();
                               done();
                           });
                       });
            });
        };
        if (conflict && activeName.startsWith(u"VyprKonnect")) {
            runNmcli({u"connection"_s, u"down"_s, activeName},
                     [this, afterDown](int, const QString &) { afterDown(); });
        } else {
            afterDown();
        }
    });
}

} // namespace vypr
