#include "nmbackendbase.h"

#include <QFile>
#include <QProcess>

#include "credentialstore.h"

namespace vypr {

using namespace Qt::Literals::StringLiterals;

static const char *kProfilePrefix = "VyprKonnect";

NmBackendBase::NmBackendBase(ServerModel *servers, QObject *parent)
    : QObject(parent), m_servers(servers)
{
    m_pollTimer.setInterval(3000);
    connect(&m_pollTimer, &QTimer::timeout, this, [this] {
        runNmcli({u"-f"_s, u"GENERAL.STATE"_s, u"connection"_s, u"show"_s, m_activeServer},
                 [this](int code, const QString &) {
                     if (code != 0) {
                         stopStatePolling();
                         m_activeServer.clear();
                         m_activeServerName.clear();
                         setState(Disconnected);
                     }
                 });
    });
}

void NmBackendBase::setState(State s)
{
    qDebug("setState %d (from %d)", int(s), int(m_state));
    if (m_state == s)
        return;
    m_state = s;
    emit stateChanged();
}

void NmBackendBase::setMessage(const QString &msg)
{
    if (m_statusMessage == msg)
        return;
    m_statusMessage = msg;
    emit statusMessageChanged();
}

void NmBackendBase::runNmcli(const QStringList &args, Continuation cont, int timeoutMs)
{
    auto *proc = new QProcess(this);
    proc->setProgram(u"nmcli"_s);
    proc->setArguments(args);
    proc->setProcessChannelMode(QProcess::MergedChannels); // errors land in `output`
    QTimer::singleShot(timeoutMs, proc, [proc] {
        if (proc->state() != QProcess::NotRunning)
            proc->kill();
    });
    connect(proc, &QProcess::finished, this, [proc, cont](int code, QProcess::ExitStatus st) {
        qDebug("nmcli %s -> exit=%d", qPrintable(proc->arguments().join(u' ')), code);
        cont(st == QProcess::NormalExit ? code : -1, QString::fromUtf8(proc->readAllStandardOutput()));
        proc->deleteLater();
    });
    connect(proc, &QProcess::errorOccurred, this, [proc](QProcess::ProcessError) {
        qWarning("nmcli failed to run: %s", qPrintable(proc->errorString()));
    });
    proc->start();
}

void NmBackendBase::beginConnect(const QString &serverName,
                                 const std::function<void(const vypr::Server &)> &proceed)
{
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
    resolveConflictThen([this, proceed] {
        proceed(m_servers->get(m_servers->indexOf(m_pendingServer)));
    });
}

void NmBackendBase::checkActiveVpnThen(
    const std::function<void(bool, const QString &)> &next)
{
    runNmcli({u"-t"_s, u"-f"_s, u"NAME,TYPE,DEVICE,STATE"_s, u"connection"_s, u"show"_s,
              u"--active"_s},
             [this, next](int code, const QString &out) {
                 if (code != 0) {
                     next(false, {});
                     return;
                 }
                 for (const QString &line : out.split(u'\n', Qt::SkipEmptyParts)) {
                     const QStringList seg = line.split(u':');
                     if (seg.size() < 4)
                         continue;
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

void NmBackendBase::resolveConflictThen(const std::function<void()> &proceed)
{
    checkActiveVpnThen([this, proceed](bool conflict, const QString &activeName) {
        if (!conflict) {
            proceed();
            return;
        }
        if (activeName.startsWith(kProfilePrefix)) {
            // Our own previous profile: switch cleanly.
            runNmcli({u"connection"_s, u"down"_s, activeName}, [this, proceed](int, const QString &) {
                proceed();
            });
        } else {
            setMessage(u"Another VPN (%1) is active — VyprVPN tunnels cannot be nested. Disconnect it first."_s.arg(activeName));
            setState(Failed);
        }
    });
}

QString NmBackendBase::profileId(const vypr::Server &server) const
{
    return u"%1 - %2"_s.arg(kProfilePrefix, server.name);
}

void NmBackendBase::startStatePolling()
{
    stopStatePolling();
    m_pollTimer.start();
}

void NmBackendBase::stopStatePolling()
{
    m_pollTimer.stop();
}

} // namespace vypr
