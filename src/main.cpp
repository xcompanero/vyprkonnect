// vyprkonnect — Qt6/QML client for VyprVPN on Linux
// Phase 1 skeleton: window shell + tray wiring comes next.
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName("vyprkonnect");
    app.setOrganizationName("vyprkonnect");
    // Plasma-native look; falls back gracefully outside KDE.
    QQuickStyle::setStyle("org.kde.desktop");

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        []() { qCritical() << "QML startup failed"; QCoreApplication::exit(1); },
        Qt::QueuedConnection);
    engine.loadFromModule("VyprKonnect", "Main");

    return app.exec();
}
