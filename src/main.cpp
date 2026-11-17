// vyprkonnect — Qt6/QML client for VyprVPN on Linux
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTimer>
#include <QVariant>
#include <QtLogging>
#include <QProcess>
#include <QMenu>
#include <QSystemTrayIcon>
#include <QAction>

using namespace Qt::Literals::StringLiterals;

#include "credentialstore.h"
#include "nmopenvpnbackend.h"
#include "nmwireguardbackend.h"
#include "servermodel.h"
#include "wanipfetcher.h"

namespace {

// Data directory resolution order:
//   1. $VYPRKONNECT_DATA_DIR   (explicit override)
//   2. compile-time VYPR_DATA_DIR (dev builds: source tree)
//   3. installed location (/usr/share/vyprkonnect/data)
QString resolveDataDir()
{
    if (const QString env = qEnvironmentVariable("VYPRKONNECT_DATA_DIR"); !env.isEmpty())
        return env;
#ifdef VYPR_DATA_DIR
    return QStringLiteral(VYPR_DATA_DIR);
#endif
    return u"/usr/share/vyprkonnect/data"_s;
}

} // namespace

// Startup hygiene: VyprKonnect profiles are recreated on every connect, so
// leftovers serve no purpose — sweep them (preserving currently-active ones
// and, of course, the user's own hand-made "Vypr *" profiles).
void sweepStaleProfiles()
{
    QProcess list;
    list.start(u"nmcli"_s, {u"-t"_s, u"-f"_s, u"NAME"_s, u"connection"_s, u"show"_s});
    list.waitForFinished(5000);
    QStringList all;
    for (const QString &line : QString::fromUtf8(list.readAllStandardOutput())
             .split(u'\n', Qt::SkipEmptyParts))
        if (line.startsWith(u"VyprKonnect "_s))
            all.append(line);

    QProcess active;
    active.start(u"nmcli"_s, {u"-t"_s, u"-f"_s, u"NAME"_s, u"connection"_s, u"show"_s, u"--active"_s});
    active.waitForFinished(5000);
    const QStringList activeNames = QString::fromUtf8(active.readAllStandardOutput())
        .split(u'\n', Qt::SkipEmptyParts);

    for (const QString &name : all) {
        if (activeNames.contains(name))
            continue;
        QProcess del;
        del.start(u"nmcli"_s, {u"connection"_s, u"delete"_s, name});
        del.waitForFinished(5000);
        if (del.exitStatus() == QProcess::NormalExit && del.exitCode() == 0)
            qInfo("swept stale profile: %s", qPrintable(name));
    }
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("vyprkonnect");
    app.setOrganizationName("vyprkonnect");
    // Plasma-native look; falls back gracefully outside KDE.
    QQuickStyle::setStyle("org.kde.desktop");

    using namespace vypr;

    ServerModel servers;
    const QString dataDir = resolveDataDir();
    if (!servers.loadFromFile(dataDir + QStringLiteral("/servers.json")))
        qWarning("Failed to load bundled server list from %s", qPrintable(dataDir));

    NmOpenVpnBackend openVpn(&servers);
    NmWireGuardBackend wireGuard(&servers);
    WanIpFetcher wanIp;
    CredentialStore &creds = CredentialStore::instance();

    // CLI test harness: --protocol <openvpn|wireguard> --connect <server> / --down.
    const QStringList args = app.arguments();
    const bool cliMode = args.contains(u"--connect"_s) || args.contains(u"--down"_s);
    const int protoIdx = args.indexOf(u"--protocol"_s);
    const bool useWg = protoIdx >= 0 && protoIdx + 1 < args.size()
        && args.at(protoIdx + 1).compare(u"wireguard"_s, Qt::CaseInsensitive) == 0;
    QString recoveredProtocol;
    NmBackendBase &backend = useWg ? static_cast<NmBackendBase &>(wireGuard)
                                   : static_cast<NmBackendBase &>(openVpn);

    if (!cliMode) {
        sweepStaleProfiles();

        // Session recovery: if a VyprKonnect tunnel is already established,
        // reflect it instead of showing a blank "Not Connected" state.
        QProcess act;
        act.start(u"nmcli"_s, {u"-t"_s, u"-f"_s, u"NAME,TYPE,DEVICE,STATE"_s,
                                u"connection"_s, u"show"_s, u"--active"_s});
        act.waitForFinished(5000);
        for (const QString &line : QString::fromUtf8(act.readAllStandardOutput())
                 .split(u'\n', Qt::SkipEmptyParts)) {
            if (!line.startsWith(u"VyprKonnect - "_s))
                continue;
            const QStringList seg = line.split(u':');
            if (seg.size() < 4)
                continue;
            const QString type = seg.at(seg.size() - 3);
            if (type != u"vpn"_s && type != u"wireguard"_s)
                continue;
            const QString name = seg.mid(0, seg.size() - 3).join(u':');
            const QString serverName = name.mid(QString(u"VyprKonnect - "_s).size());
            NmBackendBase *recovered = (type == u"wireguard"_s)
                ? static_cast<NmBackendBase *>(&wireGuard)
                : static_cast<NmBackendBase *>(&openVpn);
            recovered->recoverActiveProfile(name, serverName);
            recoveredProtocol = (type == u"wireguard"_s) ? QStringLiteral("WireGuard")
                                                         : QStringLiteral("OpenVPN");
            qInfo("recovered active session: %s (%s)", qPrintable(name), qPrintable(type));
            break;
        }
    }

    // Both backends feed the WAN-IP display (the GUI can switch protocol at will).
    QObject::connect(&openVpn, &NmBackendBase::stateChanged, &wanIp, &WanIpFetcher::refresh);
    QObject::connect(&wireGuard, &NmBackendBase::stateChanged, &wanIp, &WanIpFetcher::refresh);
    wanIp.refresh();

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("VpnBackend"), &backend);
    engine.rootContext()->setContextProperty(QStringLiteral("VpnOpenVpn"), &openVpn);
    engine.rootContext()->setContextProperty(QStringLiteral("VpnWireGuard"), &wireGuard);
    engine.rootContext()->setContextProperty(QStringLiteral("Servers"), &servers);
    engine.rootContext()->setContextProperty(QStringLiteral("Credentials"), &creds);
    engine.rootContext()->setContextProperty(QStringLiteral("WanIp"), &wanIp);
    if (!recoveredProtocol.isEmpty())
        engine.rootContext()->setContextProperty(QStringLiteral("RecoveredProtocol"), recoveredProtocol);
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        []() { qCritical() << "QML startup failed"; QCoreApplication::exit(1); },
        Qt::QueuedConnection);
    engine.loadFromModule("VyprKonnect", "Main");

    // System tray: the tunnel outlives the window, so the tray is how the user
    // recovers/observes a running session and how they quit the app.
    if (!cliMode) {
        auto *tray = new QSystemTrayIcon(QIcon::fromTheme(u"network-vpn"_s), &app);
        auto *menu = new QMenu();
        QAction *showAct = menu->addAction(QObject::tr("Show VyprKonnect"));
        menu->addSeparator();
        QAction *discAct = menu->addAction(QObject::tr("Disconnect"));
        menu->addSeparator();
        QAction *quitAct = menu->addAction(QObject::tr("Quit"));
        tray->setContextMenu(menu);

        // The tray must reflect whichever tunnel is ACTUALLY up — not the
        // backend selected at startup (the GUI can switch protocol freely).
        const auto activeBackend = [&backend, &wireGuard, &openVpn]() -> NmBackendBase & {
            if (wireGuard.connected() || wireGuard.busy())
                return wireGuard;
            if (openVpn.connected() || openVpn.busy())
                return openVpn;
            return backend;
        };
        const auto refreshTray = [tray, discAct, &activeBackend] {
            NmBackendBase &b = activeBackend();
            tray->setToolTip(b.stateLabel());
            discAct->setEnabled(b.canDisconnect());
        };
        QObject::connect(&openVpn, &NmBackendBase::stateChanged, tray, refreshTray);
        QObject::connect(&openVpn, &NmBackendBase::statusMessageChanged, tray, refreshTray);
        QObject::connect(&wireGuard, &NmBackendBase::stateChanged, tray, refreshTray);
        QObject::connect(&wireGuard, &NmBackendBase::statusMessageChanged, tray, refreshTray);
        QObject::connect(showAct, &QAction::triggered, [&engine] {
            for (QObject *o : engine.rootObjects()) {
                o->setProperty("visible", true);
                o->setProperty("visibility", 4); // maximized
            }
        });
        QObject::connect(discAct, &QAction::triggered, [&activeBackend] {
            NmBackendBase &b = activeBackend();
            if (b.canDisconnect())
                b.disconnect();
        });
        QObject::connect(quitAct, &QAction::triggered, &app, &QCoreApplication::quit);
        tray->show();
        app.setQuitOnLastWindowClosed(false); // closing the window keeps the tunnel & tray
    }

    if (cliMode) {
        const bool isDown = args.contains(u"--down"_s);
        const int nameIdx = isDown ? -1 : args.indexOf(u"--connect"_s) + 1;
        QObject::connect(&backend, &NmBackendBase::stateChanged, [&backend, isDown] {
            qInfo("state: %s (%s)", qPrintable(QVariant::fromValue(backend.state()).toString()),
                  qPrintable(backend.statusMessage()));
            if (backend.state() == NmBackendBase::Connected
                || backend.state() == NmBackendBase::Failed
                || (isDown && backend.state() == NmBackendBase::Disconnected)) {
                QCoreApplication::exit(backend.state() == NmBackendBase::Failed ? 2 : 0);
            }
        });
        if (isDown) {
            backend.disconnect();
        } else if (nameIdx < args.size()) {
            backend.connectToServer(args.at(nameIdx));
        } else {
            qCritical("--connect requires a server name");
            return 1;
        }
        return app.exec();
    }

    QTimer::singleShot(1500, [] { qInfo("alive after 1.5s"); });
    const int rc = app.exec();
    qInfo("exec returned %d", rc);
    return rc;
}
