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
#include "servermodel.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName("vyprkonnect");
    app.setOrganizationName("vyprkonnect");
    // Plasma-native look; falls back gracefully outside KDE.
    QQuickStyle::setStyle("org.kde.desktop");

    using namespace vypr;

    ServerModel servers;
    const QString dataDir = QStringLiteral(VYPR_DATA_DIR);
    if (!servers.loadFromFile(dataDir + QStringLiteral("/servers.json")))
        qWarning("Failed to load bundled server list from %s", qPrintable(dataDir));

    NmOpenVpnBackend backend(&servers);
    CredentialStore &creds = CredentialStore::instance();

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("VpnBackend"), &backend);
    engine.rootContext()->setContextProperty(QStringLiteral("Servers"), &servers);
    engine.rootContext()->setContextProperty(QStringLiteral("Credentials"), &creds);
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        []() { qCritical() << "QML startup failed"; QCoreApplication::exit(1); },
        Qt::QueuedConnection);
    engine.loadFromModule("VyprKonnect", "Main");
    qInfo("root objects: %lld", (long long)engine.rootObjects().size());

    // CLI test harness: --connect <server> / --down, non-interactive.
    const QStringList args = app.arguments();
    if (args.contains(u"--connect"_s) || args.contains(u"--down"_s)) {
        const bool isDown = args.contains(u"--down"_s);
        const int nameIdx = isDown ? -1 : args.indexOf(u"--connect"_s) + 1;
        QObject::connect(&backend, &NmOpenVpnBackend::stateChanged, [&backend, isDown] {
            const QString s = QVariant::fromValue(backend.state()).toString();
            qInfo("state: %s (%s)", qPrintable(s), qPrintable(backend.statusMessage()));
            if (backend.state() == NmOpenVpnBackend::Connected
                || backend.state() == NmOpenVpnBackend::Failed
                || (isDown && backend.state() == NmOpenVpnBackend::Disconnected)) {
                QCoreApplication::exit(backend.state() == NmOpenVpnBackend::Failed ? 2 : 0);
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
