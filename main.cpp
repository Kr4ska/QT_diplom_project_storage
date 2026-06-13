#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include "databasemanager.h"
#include "layoutoptimizer.h"
#include "yandexapimanager.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    DatabaseManager dbManager;
   // dbManager.connectToDatabase("C:/Users/Andrey/Desktop/ДипломБД.accdb");
    qmlRegisterType<LayoutOptimizer>("com.warehouse.optimizer", 1, 0, "LayoutOptimizer");
    QQmlApplicationEngine engine;

    YandexApiManager apiManager;

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.rootContext()->setContextProperty("dbManager", &dbManager);
    engine.rootContext()->setContextProperty("apiManager", &apiManager);

    // Qt 6.4 does not have loadFromModule. Loading directly via qrc and module URI.
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/diplom_project_storage/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;

    return QCoreApplication::exec();
}
