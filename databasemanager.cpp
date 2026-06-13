#include "databasemanager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDir>
#include <QDebug>
#include <QUrl>
#include <QDateTime>
#include <QFileInfo>
#include <QFile>
#include <QTextStream>
#include <QCoreApplication>
#include <QSqlRecord>

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

QString DatabaseManager::readJsonFile(const QString &fileName) {
    // Ищем файл рядом с исполняемым файлом или в текущей рабочей директории
    QString filePath = QCoreApplication::applicationDirPath() + "/" + fileName;
    if (!QFile::exists(filePath)) {
        filePath = QDir::currentPath() + "/" + fileName; // Фолбэк на рабочую директорию
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "❌ Failed to open JSON file for reading:" << filePath;
        return "";
    }
    return QString(file.readAll());
}

bool DatabaseManager::writeJsonFile(const QString &fileName, const QString &jsonString) {
    QString filePath = QCoreApplication::applicationDirPath() + "/" + fileName;
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qDebug() << "❌ Failed to open JSON file for writing:" << filePath;
        return false;
    }
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << jsonString;
    return true;
}

QString DatabaseManager::getAllEquipmentCatalog() {
    if (!m_db.isOpen()) return "База данных не подключена.";

    QSqlQuery query(m_db);
    // Мы запрашиваем ModelID, габариты и EquipmentCategory
    query.prepare("SELECT e.ModelID, e.DimWidth, e.DimLength, e.EquipCategory, "
                  "(SELECT count(*) FROM Machine_Specs m WHERE m.ModelID = e.ModelID) as is_machine, "
                  "(SELECT count(*) FROM Rack_Specs r WHERE r.ModelID = e.ModelID) as is_rack, "
                  "(SELECT count(*) FROM Robot_Specs ro WHERE ro.ModelID = e.ModelID) as is_robot "
                  "FROM Equipment_base e");

    if (!query.exec()) {
        qDebug() << "Error fetching equipment catalog:" << query.lastError().text();
        // Fallback на старый запрос без подзапросов, если синтаксис Access не поддержит
        query.prepare("SELECT ModelID, DimWidth, DimLength, EquipCategory FROM Equipment_base");
        if (!query.exec()) {
            return "Ошибка при чтении каталога оборудования.";
        }
    }

    QString catalog = "Каталог доступного оборудования:\n";
    catalog += "---------------------------------\n";
    while (query.next()) {
        QString modelId = query.value("ModelID").toString();
        double w = query.value("DimWidth").toDouble();
        double l = query.value("DimLength").toDouble();

        QString type = "unknown";

        // Сначала проверяем флаги из связанных таблиц
        if (query.record().contains("is_machine")) {
            if (query.value("is_machine").toInt() > 0) {
                type = "machine";
            } else if (query.value("is_rack").toInt() > 0) {
                type = "rack";
            } else if (query.value("is_robot").toInt() > 0) {
                type = "robot";
            }
        }

        // Если тип не удалось определить по таблицам или подзапросы не отработали,
        // пробуем посмотреть EquipCategory
        if (type == "unknown" && query.record().contains("EquipCategory")) {
            QString cat = query.value("EquipCategory").toString().toLower();
            if (!cat.isEmpty()) {
                type = query.value("EquipCategory").toString(); // Оригинальный регистр
            }
        }

        catalog += QString("- ID: %1 | Тип: %2 | Габариты (ШxД): %3x%4 м\n")
                    .arg(modelId).arg(type).arg(w).arg(l);
    }
    catalog += "---------------------------------\n";
    return catalog;
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