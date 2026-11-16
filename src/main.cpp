// vyprkonnect — Qt6/QML client for VyprVPN on Linux
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTimer>
#include <QVariant>
#include <QtLogging>

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

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
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
    NmBackendBase &backend = useWg ? static_cast<NmBackendBase &>(wireGuard)
                                   : static_cast<NmBackendBase &>(openVpn);

    QObject::connect(&backend, &NmBackendBase::stateChanged, &wanIp, &WanIpFetcher::refresh);
    wanIp.refresh();

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("VpnBackend"), &backend);
    engine.rootContext()->setContextProperty(QStringLiteral("VpnOpenVpn"), &openVpn);
    engine.rootContext()->setContextProperty(QStringLiteral("VpnWireGuard"), &wireGuard);
    engine.rootContext()->setContextProperty(QStringLiteral("Servers"), &servers);
    engine.rootContext()->setContextProperty(QStringLiteral("Credentials"), &creds);
    engine.rootContext()->setContextProperty(QStringLiteral("WanIp"), &wanIp);
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        []() { qCritical() << "QML startup failed"; QCoreApplication::exit(1); },
        Qt::QueuedConnection);
    engine.loadFromModule("VyprKonnect", "Main");

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
