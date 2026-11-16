#include "nmopenvpnbackend.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTimerEvent>

#include "credentialstore.h"

namespace vypr {

using namespace Qt::Literals::StringLiterals;

static const char *kProfilePrefix = "VyprKonnect";

NmOpenVpnBackend::NmOpenVpnBackend(ServerModel *servers, QObject *parent)
    : QObject(parent), m_servers(servers)
{
    m_pollTimer.setInterval(3000);
    connect(&m_pollTimer, &QTimer::timeout, this, [this] {
        runNmcli({u"-f"_s, u"GENERAL.STATE"_s, u"connection"_s, u"show"_s, m_activeServer},
                 [this](int code, const QString &out) {
                     if (code != 0) {
                         // Profile gone or inactive: treat as disconnected.
                         stopStatePolling();
                         setState(Disconnected);
                         m_activeServer.clear();
                         emit stateChanged();
                     }
                 });
    });
}

void NmOpenVpnBackend::setState(State s)
{
    qDebug("setState %d (from %d)", int(s), int(m_state));
    if (m_state == s)
        return;
    m_state = s;
    emit stateChanged();
}

void NmOpenVpnBackend::setMessage(const QString &msg)
{
    if (m_statusMessage == msg)
        return;
    m_statusMessage = msg;
    emit statusMessageChanged();
}

void NmOpenVpnBackend::runNmcli(const QStringList &args, Continuation cont, int timeoutMs)
{
    auto *proc = new QProcess(this);
    proc->setProgram(u"nmcli"_s);
    proc->setArguments(args);
    QTimer::singleShot(timeoutMs, proc, [proc] {
        if (proc->state() != QProcess::NotRunning) {
            proc->kill();
        }
    });
    connect(proc, &QProcess::finished, this, [this, proc, cont, args](int code, QProcess::ExitStatus st) {
        qDebug("nmcli %s -> exit=%d", qPrintable(args.join(u' ')), code);
        cont(st == QProcess::NormalExit ? code : -1, QString::fromUtf8(proc->readAllStandardOutput()));
        proc->deleteLater();
    });
    connect(proc, &QProcess::errorOccurred, this, [proc](QProcess::ProcessError) {
        qWarning("nmcli failed to run: %s", qPrintable(proc->errorString()));
    });
    proc->start();
}

void NmOpenVpnBackend::connectToServer(const QString &serverName)
{
    qDebug("connectToServer(%s) entered", qPrintable(serverName));
    if (m_state == Connecting || m_state == Disconnecting)
        return;

    const int idx = m_servers->indexOf(serverName);
    if (idx < 0) {
        setMessage(u"Unknown server: %1"_s.arg(serverName));
        setState(Failed);
        return;
    }
    if (CredentialStore::instance().username().isEmpty()
        || CredentialStore::instance().password().isEmpty()) {
        setMessage(u"Account credentials are not configured."_s);
        setState(Failed);
        return;
    }

    m_pendingServer = serverName;
    setMessage(QString());
    setState(Connecting);

    // Rule 4: detect an active VPN and resolve conflicts deterministically.
    checkActiveVpnThen([this](bool conflict, const QString &activeName) {
        if (!conflict) {
            ensureProfileAndConnect(m_servers->get(m_servers->indexOf(m_pendingServer)));
            return;
        }
        if (activeName.startsWith(kProfilePrefix)) {
            // Our own previous profile: switch cleanly.
            runNmcli({u"connection"_s, u"down"_s, activeName}, [this](int, const QString &) {
                ensureProfileAndConnect(m_servers->get(m_servers->indexOf(m_pendingServer)));
            });
        } else {
            setMessage(u"Another VPN (%1) is active — VyprVPN tunnels cannot be nested. Disconnect it first."_s.arg(activeName));
            setState(Failed);
        }
    });
}

void NmOpenVpnBackend::disconnect()
{
    if (m_state == Disconnecting)
        return;
    setState(Disconnecting);
    stopStatePolling();
    // Stateless scan: works even when this process never initiated the tunnel
    // (fresh app start, CLI harness). Deactivates the active VyprKonnect profile.
    checkActiveVpnThen([this](bool conflict, const QString &activeName) {
        if (!conflict || !activeName.startsWith(kProfilePrefix)) {
            m_activeServer.clear();
            setState(Disconnected);
            emit stateChanged();
            return;
        }
        runNmcli({u"connection"_s, u"down"_s, activeName}, [this, activeName](int, const QString &) {
            if (m_activeServer == activeName)
                m_activeServer.clear();
            setState(Disconnected);
            emit stateChanged();
        });
    });
}

QString NmOpenVpnBackend::wanIpAddress() const
{
    // Placeholder: the QML layer will call this through a small HTTP helper later.
    return {};
}

void NmOpenVpnBackend::checkActiveVpnThen(
    const std::function<void(bool, const QString &)> &next)
{
    runNmcli({u"-t"_s, u"-f"_s, u"NAME,TYPE,DEVICE,STATE"_s, u"connection"_s, u"show"_s,
              u"--active"_s},
             [this, next](int code, const QString &out) {
                 if (code != 0) {
                     next(false, {});
                     return;
                 }
                 qDebug("active connections scan:\n%s", qPrintable(out));
                 for (const QString &line : out.split(u'\n', Qt::SkipEmptyParts)) {
                     const QStringList seg = line.split(u':');
                     if (seg.size() < 4)
                         continue;
                     // Last three segments are TYPE:DEVICE:STATE; NAME may contain colons.
                     const QString type = seg.at(seg.size() - 3);
                     if (type == u"vpn"_s || type == u"wireguard"_s) {
                         const QString name = seg.mid(0, seg.size() - 3).join(u':');
                         next(true, name);
                         return;
                     }
                 }
                 next(false, {});
             });
}

QString NmOpenVpnBackend::profileId(const vypr::Server &server) const
{
    return u"%1 - %2"_s.arg(kProfilePrefix, server.name);
}

void NmOpenVpnBackend::ensureProfileAndConnect(const vypr::Server &server)
{
    qDebug("ensureProfileAndConnect: %s", qPrintable(server.name));
    const QString id = profileId(server);
    const QString ovpnPath = u"%1/ovpn/%2.ovpn"_s.arg(QByteArrayLiteral(VYPR_DATA_DIR).constData(), server.name);

    // Rule 1: NM's openvpn plugin rejects `compress stub-v2` in imports.
    QFile src(ovpnPath);
    if (!src.open(QIODevice::ReadOnly)) {
        setState(Failed);
        setMessage(u"Missing bundled config for %1"_s.arg(server.name));
        return;
    }
    QString cfg = QString::fromUtf8(src.readAll());
    cfg.replace(QRegularExpression(u"^compress stub-v2$"_s, QRegularExpression::MultilineOption),
                u"compress"_s);

    auto *tmpDir = new QTemporaryDir;
    if (!tmpDir->isValid()) {
        setState(Failed);
        setMessage(u"Internal error: cannot create temporary files."_s);
        return;
    }
    const QString tmpOvpn = tmpDir->filePath(server.name + u".ovpn"_s);
    QFile out(tmpOvpn);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
        setState(Failed);
        setMessage(u"Internal error: cannot write temporary config."_s);
        return;
    }
    out.write(cfg.toUtf8());
    out.close();

    // Remove any stale profile with the same id, then import fresh. This is
    // self-healing: config updates in the bundled set propagate on next connect.
    runNmcli({u"connection"_s, u"delete"_s, id}, [this, id, tmpOvpn, server, tmpDir](int, const QString &) {
        runNmcli({u"connection"_s, u"import"_s, u"type"_s, u"openvpn"_s, u"file"_s, tmpOvpn},
                 [this, id, tmpOvpn, server, tmpDir](int code, const QString &out) {
                     if (code != 0) {
                         setState(Failed);
                         setMessage(u"Failed to import VPN profile: %1"_s.arg(out.trimmed()));
                         delete tmpDir;
                         return;
                     }
                     // Import names the profile after the .ovpn file.
                     const QString importedName = server.name;
                     const QString user = CredentialStore::instance().username();

                     // Rules 1-3: username, asymmetric compression, no autoconnect.
                     // (Chained: nmcli accepts only one `+vpn.data` assignment per call.)
                     runNmcli({u"connection"_s, u"modify"_s, importedName,
                               u"connection.id"_s, id,
                               u"connection.autoconnect"_s, u"no"_s},
                              [this, id, importedName, user, tmpOvpn, tmpDir](int c1, const QString &o1) {
                                  if (c1 != 0) {
                                      setState(Failed);
                                      setMessage(u"Failed to configure VPN profile: %1"_s.arg(o1.trimmed()));
                                      delete tmpDir;
                                      return;
                                  }
                                  runNmcli({u"connection"_s, u"modify"_s, id,
                                            u"+vpn.data"_s, u"username=%1"_s.arg(user)},
                                           [this, id, importedName, tmpOvpn, tmpDir](int c2, const QString &o2) {
                                               if (c2 != 0) {
                                                   setState(Failed);
                                                   setMessage(u"Failed to configure VPN profile: %1"_s.arg(o2.trimmed()));
                                                   delete tmpDir;
                                                   return;
                                               }
                                               runNmcli({u"connection"_s, u"modify"_s, id,
                                                         u"+vpn.data"_s, u"compress=asymmetric"_s},
                                                        [this, id, tmpOvpn, tmpDir](int c3, const QString &o3) {
                                                            finishProfileSetup(id, c3, o3, tmpDir);
                                                        });
                                           });
                              });
                 });
    });
}

void NmOpenVpnBackend::finishProfileSetup(const QString &id, int code, const QString &out,
                                          QTemporaryDir *tmpDir)
{
    if (code != 0) {
    setState(Failed);
    setMessage(u"Failed to configure VPN profile: %1"_s.arg(out.trimmed()));
    delete tmpDir;
    return;
    }
    // Rule 5: hand credentials at activation via passwd-file.
    auto *passFile = new QFile(tmpDir->filePath(u"pw"_s));
    passFile->open(QIODevice::WriteOnly | QIODevice::Truncate);
    passFile->write((u"vpn.secrets.password:%1\n"_s
    .arg(CredentialStore::instance().password()))
    .toUtf8());
    passFile->close();
    passFile->setPermissions(QFile::ReadOwner | QFile::WriteOwner);

    m_activeServer = id;
    emit stateChanged();
    runNmcli({u"connection"_s, u"up"_s, id,
    u"passwd-file"_s, passFile->fileName()},
    [this, passFile, tmpDir](int upcode, const QString &upout) {
    passFile->remove();
    if (upcode == 0) {
    setState(Connected);
    setMessage(QString());
    startStatePolling();
    } else {
    m_activeServer.clear();
    // Distinguish auth failure from unreachable server.
    // Journal-level detail arrives via NM logs; surface
    // an honest generic error here.
    setState(Failed);
    setMessage(u"Connection attempt failed: %1"_s.arg(upout.trimmed()));
    }
    delete tmpDir;
    });
    }

void NmOpenVpnBackend::startStatePolling()
{
    stopStatePolling();
    m_pollTimer.start();
}

void NmOpenVpnBackend::stopStatePolling()
{
    m_pollTimer.stop();
}

} // namespace vypr
