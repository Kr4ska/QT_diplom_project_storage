#include "databasemanager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDir>
#include <QDebug>
#include <QUrl>
#include <QDateTime>
#include <QFileInfo>

DatabaseManager::DatabaseManager(QObject *parent) : QObject(parent) {}

DatabaseManager::~DatabaseManager() {
    removeDatabase();
}

void DatabaseManager::removeDatabase() {
    if (m_db.isValid()) {
        QString name = m_db.connectionName();
        if (m_db.isOpen()) m_db.close();
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase(name);
    }
}

QVariantMap DatabaseManager::getEquipmentInfo(const QString &modelId) {
    QVariantMap result;
    if (!m_db.isOpen()) return result;

    QSqlQuery query(m_db);
    // Предполагаем, что ты добавил столбец image_url в Access
    query.prepare("SELECT ModelID, image_url, DimWidth, DimLength FROM Equipment_base WHERE ModelID = :id");
    query.bindValue(":id", modelId);

    if (query.exec() && query.next()) {
        QString rawPath = query.value("image_url").toString();
        QFileInfo dbFileInfo(m_db.databaseName().split("DBQ=").last().split(";").first());
        QString dbDirectory = dbFileInfo.absolutePath(); // Путь к папке

        // Склеиваем: Путь_к_папке + / + Относительный_путь_из_БД
        // QDir::cleanPath уберет лишние точки и слэши типа /./
        QString fullPath = QDir::cleanPath(dbDirectory + "/" + rawPath);

        result["modelId"] = query.value("ModelID").toString();
        result["imageUrl"] = fullPath; // Отправляем в QML уже готовый полный путь

        result["width"] = query.value("DimWidth").toDouble();
        result["length"] = query.value("DimLength").toDouble();

        qDebug() << "Full Image Path:" << fullPath;
    } else {
        qDebug() << "Error fetching equipment info:" << query.lastError().text();
    }
    return result;
}

bool DatabaseManager::connectToDatabase(const QString &dbPath) {
    removeDatabase();
    QString path = dbPath;
    if (path.startsWith("file:")) path = QUrl(dbPath).toLocalFile();
    path = QDir::toNativeSeparators(path);

    m_db = QSqlDatabase::addDatabase("QODBC", QString("Conn_%1").arg(QDateTime::currentMSecsSinceEpoch()));
    QString connStr = QString("Driver={Microsoft Access Driver (*.mdb, *.accdb)};DBQ=%1;").arg(path);
    m_db.setDatabaseName(connStr);

    if (!m_db.open()) {
        qDebug() << "❌ Connection error:" << m_db.lastError().text();
        return false;
    }
    qDebug() << "✅ Connected to Access database";

    emit databaseConnected();

    return true;
}

int DatabaseManager::createProjectWithFullData(const QString &title, double width, double height, const QVariantMap &allData) {
    if (!m_db.isOpen()) return -1;

    m_db.transaction();
    QSqlQuery q(m_db);

    // 1. Создаем проект
    q.prepare("INSERT INTO Projects (ProjectTitle, AreaWidth, AreaLength) VALUES (:t, :w, :l)");
    q.bindValue(":t", title);
    q.bindValue(":w", width);
    q.bindValue(":l", height);

    if (!q.exec()) {
        qDebug() << "❌ Projects table error:" << q.lastError().text();
        m_db.rollback();
        return -1;
    }

    int pid = -1;
    if (q.exec("SELECT @@IDENTITY") && q.next()) pid = q.value(0).toInt();
    if (pid <= 0) { m_db.rollback(); return -1; }

    // 2. Оборудование (Layout_Results)
    QVariantList layout = allData["layout"].toList();
    // Используем именованные привязки для надежности
    q.prepare("INSERT INTO Layout_Results (ParentProjectID, ModelID, CoordX, CoordY, AngleRotation) "
              "VALUES (:pid, :mid, :x, :y, :angle)");

    for (const QVariant &v : layout) {
        QVariantMap m = v.toMap();
        q.bindValue(":pid", pid);
        q.bindValue(":mid", m["ModelID"].toString());
        q.bindValue(":x", m["CoordX"].toDouble());
        q.bindValue(":y", m["CoordY"].toDouble());
        q.bindValue(":angle", m["AngleRotation"].toDouble());

        if(!q.exec()) {
            qDebug() << "❌ Layout entry error:" << q.lastError().text();
        }
    }

    // 3. Узлы (Robot_Network_Nodes)
    QMap<int, int> nodeMap; // Для связи старых ID из JSON с новыми из БД
    QVariantList nodes = allData["nodes"].toList();
    q.prepare("INSERT INTO Robot_Network_Nodes (ParentProjectID, NodeX, NodeY, MarkerType) "
              "VALUES (:pid, :nx, :ny, :mt)");

    for (const QVariant &v : nodes) {
        QVariantMap n = v.toMap();
        q.bindValue(":pid", pid);
        q.bindValue(":nx", n["NodeX"].toDouble());
        q.bindValue(":ny", n["NodeY"].toDouble());
        q.bindValue(":mt", n["MarkerType"].toString());

        if (q.exec()) {
            QSqlQuery ident(m_db);
            if (ident.exec("SELECT @@IDENTITY") && ident.next()) {
                nodeMap[n["NodeID"].toInt()] = ident.value(0).toInt();
            }
        } else {
            qDebug() << "❌ Nodes error:" << q.lastError().text();
        }
    }

    // 4. Ребра (Robot_Network_Edges)
    QVariantList edges = allData["edges"].toList();
    q.prepare("INSERT INTO Robot_Network_Edges (ParentProjectID, StartNodeID, EndNodeID, TwoWayTraffic) "
              "VALUES (:pid, :sn, :en, :tw)");

    for (const QVariant &v : edges) {
        QVariantMap e = v.toMap();
        int dbStart = nodeMap.value(e["StartNodeID"].toInt(), -1);
        int dbEnd = nodeMap.value(e["EndNodeID"].toInt(), -1);

        if (dbStart != -1 && dbEnd != -1) {
            q.bindValue(":pid", pid);
            q.bindValue(":sn", dbStart);
            q.bindValue(":en", dbEnd);
            q.bindValue(":tw", e["TwoWayTraffic"].toBool() ? 1 : 0);
            if(!q.exec()) {
                qDebug() << "❌ Edges error:" << q.lastError().text();
            }
        }
    }

    if (m_db.commit()) {
        qDebug() << "✅ Все данные успешно записаны в проект ID:" << pid;
        return pid;
    }

    m_db.rollback();
    return -1;
}