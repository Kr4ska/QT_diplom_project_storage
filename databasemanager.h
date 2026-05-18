#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <QObject>
#include <QSqlDatabase>
#include <QVariantMap>
#include <QVariantList>

class DatabaseManager : public QObject {
    Q_OBJECT
public:
    explicit DatabaseManager(QObject *parent = nullptr);
    ~DatabaseManager();

    Q_INVOKABLE bool connectToDatabase(const QString &dbPath);
    void removeDatabase();

    Q_INVOKABLE QVariantMap getEquipmentInfo(const QString &modelId);

    // Сохранение проекта: Оборудование + Узлы + Ребра
    Q_INVOKABLE int createProjectWithFullData(const QString &title, double width, double height, const QVariantMap &allData);

signals:
    // Тот самый сигнал, который ждет QML Connections
    void databaseConnected();
    void connectionError(const QString &message);

private:
    QSqlDatabase m_db;
};

#endif // DATABASEMANAGER_H