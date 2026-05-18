#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include "databasemanager.h"
#include "layoutoptimizer.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    DatabaseManager dbManager;
   // dbManager.connectToDatabase("C:/Users/Andrey/Desktop/ДипломБД.accdb");
    qmlRegisterType<LayoutOptimizer>("com.warehouse.optimizer", 1, 0, "LayoutOptimizer");
    QQmlApplicationEngine engine;

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.rootContext()->setContextProperty("dbManager", &dbManager);
    engine.loadFromModule("diplom_project_storage", "Main");

    return QCoreApplication::exec();
}
