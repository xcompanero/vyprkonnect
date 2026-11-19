#include "nmopenvpnbackend.h"

#include <QFile>
#include <QRegularExpression>

#include "credentialstore.h"
#include "datadir.h"

namespace vypr {

using namespace Qt::Literals::StringLiterals;

NmOpenVpnBackend::NmOpenVpnBackend(ServerModel *servers, QObject *parent)
    : NmBackendBase(servers, parent)
{
}

void NmOpenVpnBackend::connectToServer(const QString &serverName)
{
    beginConnect(serverName, [this](const vypr::Server &server) {
        ensureProfileAndConnect(server);
    });
}

void NmOpenVpnBackend::disconnect()
{
    if (m_state == Disconnecting)
        return;
    setState(Disconnecting);
    stopStatePolling();
    // Stateless scan: works even when this process never initiated the tunnel.
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
            setState(Disconnected);
            emit stateChanged();
        });
    });
}

void NmOpenVpnBackend::ensureProfileAndConnect(const vypr::Server &server)
{
    const QString id = profileId(server);
    const QString ovpnPath = u"%1/ovpn/%2.ovpn"_s.arg(vypr::resolveDataDir(), server.name);

    // Rule 1: NM's openvpn plugin rejects `compress stub-v2` in imports.
    QFile src(ovpnPath);
    if (!src.open(QIODevice::ReadOnly)) {
        setMessage(u"Missing bundled config for %1"_s.arg(server.name));
        setState(Failed);
        return;
    }
    QString cfg = QString::fromUtf8(src.readAll());
    cfg.replace(QRegularExpression(u"^compress stub-v2$"_s, QRegularExpression::MultilineOption),
                u"compress"_s);

    auto *tmpDir = new QTemporaryDir;
    if (!tmpDir->isValid()) {
        setMessage(u"Internal error: cannot create temporary files."_s);
        setState(Failed);
        return;
    }
    // NM names the imported profile after the .ovpn file: keep them aligned.
    const QString tmpOvpn = tmpDir->filePath(server.name + u".ovpn"_s);
    QFile out(tmpOvpn);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
        setMessage(u"Internal error: cannot write temporary config."_s);
        setState(Failed);
        return;
    }
    out.write(cfg.toUtf8());
    out.close();

    // Remove any stale profile with the same id, then import fresh (self-healing:
    // config updates in the bundled set propagate on next connect).
    runNmcli({u"connection"_s, u"delete"_s, id}, [this, id, tmpOvpn, server, tmpDir](int, const QString &) {
        runNmcli({u"connection"_s, u"import"_s, u"type"_s, u"openvpn"_s, u"file"_s, tmpOvpn},
                 [this, id, server, tmpDir](int code, const QString &out) {
                     if (code != 0) {
                         setMessage(u"Failed to import VPN profile: %1"_s.arg(out.trimmed()));
                         setState(Failed);
                         delete tmpDir;
                         return;
                     }
                     const QString importedName = server.name;
                     const QString user = CredentialStore::instance().username();

                     // Rules: username, asymmetric compression, no autoconnect.
                     // (Chained: nmcli accepts only one `+vpn.data` assignment per call.)
                     runNmcli({u"connection"_s, u"modify"_s, importedName,
                               u"connection.id"_s, id,
                               u"connection.autoconnect"_s, u"no"_s},
                              [this, id, importedName, user, tmpDir](int c1, const QString &o1) {
                                  if (c1 != 0) {
                                      setMessage(u"Failed to configure VPN profile: %1"_s.arg(o1.trimmed()));
                                      setState(Failed);
                                      delete tmpDir;
                                      return;
                                  }
                                  runNmcli({u"connection"_s, u"modify"_s, id,
                                            u"+vpn.data"_s, u"username=%1"_s.arg(user)},
                                           [this, id, tmpDir](int c2, const QString &o2) {
                                               if (c2 != 0) {
                                                   setMessage(u"Failed to configure VPN profile: %1"_s.arg(o2.trimmed()));
                                                   setState(Failed);
                                                   delete tmpDir;
                                                   return;
                                               }
                                               runNmcli({u"connection"_s, u"modify"_s, id,
                                                         u"+vpn.data"_s, u"compress=asymmetric"_s},
                                                        [this, id, tmpDir](int c3, const QString &o3) {
                                                            finishProfileSetup(id, u""_s, c3, o3, tmpDir);
                                                        });
                                           });
                              });
                 });
    });
}

void NmOpenVpnBackend::finishProfileSetup(const QString &id, const QString &, int code,
                                          const QString &out, QTemporaryDir *tmpDir)
{
    if (code != 0) {
        setMessage(u"Failed to configure VPN profile: %1"_s.arg(out.trimmed()));
        setState(Failed);
        delete tmpDir;
        return;
    }
    // Credentials handed at activation via passwd-file (0600, removed after).
    auto *passFile = new QFile(tmpDir->filePath(u"pw"_s));
    passFile->open(QIODevice::WriteOnly | QIODevice::Truncate);
    passFile->write((u"vpn.secrets.password:%1\n"_s
                         .arg(CredentialStore::instance().password()))
                        .toUtf8());
    passFile->close();
    passFile->setPermissions(QFile::ReadOwner | QFile::WriteOwner);

    m_activeServer = id;
    m_activeServerName = m_servers->get(m_servers->indexOf(m_pendingServer)).name;
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
                     m_activeServerName.clear();
                     setMessage(u"Connection attempt failed: %1"_s.arg(upout.trimmed()));
                     setState(Failed);
                 }
                 delete tmpDir;
             });
}

} // namespace vypr
