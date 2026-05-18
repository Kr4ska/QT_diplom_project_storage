#include "layoutoptimizer.h"
#include "databasemanager.h" // Твой класс БД
#include <limits>
#include <QDebug>
#include <QRandomGenerator>
#include <QtConcurrent/QtConcurrent>
#include <QThreadPool>

LayoutOptimizer::LayoutOptimizer(QObject *parent) : QObject(parent) {}

void LayoutOptimizer::setEnvironment(const WallConstraints& walls, const QVector<PathNode>& nodes, const QVector<PathEdge>& edges) {
    m_walls = walls;
    m_nodes = nodes;
    m_edges = edges;
}

// ---------------- ОБОГАЩЕНИЕ ДАННЫХ ----------------

void LayoutOptimizer::prepareLayout(const QVariantList &rawLayout, DatabaseManager *dbManager) {
    m_layout.clear();

    for (const QVariant &item : rawLayout) {
        QVariantMap objMap = item.toMap();
        QString modelId = objMap["ModelID"].toString();

        // Идем в БД за размерами
        QVariantMap dbData = dbManager->getEquipmentInfo(modelId);

        WarehouseObject obj;
        obj.instanceId = objMap["InstanceID"].toInt();
        obj.x = objMap["CoordX"].toDouble();
        obj.y = objMap["CoordY"].toDouble();
        obj.angle = objMap["AngleRotation"].toDouble();
        obj.type = objMap["Type"].toString();

        // Размеры из базы (с защитой от нулей)
        obj.w = dbData["width"].toDouble() > 0 ? dbData["width"].toDouble() : 1.0;
        obj.l = dbData["length"].toDouble() > 0 ? dbData["length"].toDouble() : 1.0;

        // Если это не станок/стеллаж, фиксируем его
        obj.isStatic = (obj.type != "machine" && obj.type != "rack");

        m_layout.append(obj);
    }
    qDebug() << "✅ Layout prepared. Total objects:" << m_layout.size();
}

// ---------------- МАТЕМАТИКА SAT ----------------

QVector<QPointF> LayoutOptimizer::getAxes(const QVector<QPointF>& v) const {
    QVector<QPointF> axes;
    for (int i = 0; i < v.size(); ++i) {
        QPointF p1 = v[i];
        QPointF p2 = v[(i + 1) % v.size()];
        QPointF edge = p2 - p1;
        axes << QPointF(-edge.y(), edge.x()); // Нормаль
    }
    return axes;
}

void LayoutOptimizer::project(const QVector<QPointF>& v, const QPointF& axis, double& min, double& max) const {
    min = std::numeric_limits<double>::max();
    max = std::numeric_limits<double>::lowest();
    for (const auto& p : v) {
        double dot = p.x() * axis.x() + p.y() * axis.y();
        if (dot < min) min = dot;
        if (dot > max) max = dot;
    }
}

bool LayoutOptimizer::isOverlapping(const WarehouseObject& a, const WarehouseObject& b) const {
    QVector<QPointF> v1 = a.getVertices();
    QVector<QPointF> v2 = b.getVertices();

    QVector<QPointF> axes = getAxes(v1);
    axes.append(getAxes(v2));

    for (const auto& axis : axes) {
        double min1, max1, min2, max2;
        project(v1, axis, min1, max1);
        project(v2, axis, min2, max2);

        if (max1 < min2 || max2 < min1) return false; // Найдена разделяющая ось
    }
    return true; // Пересекаются
}

// ---------------- РАСЧЕТ ЭНЕРГИИ ----------------

double LayoutOptimizer::calculateEnergy(const QVector<WarehouseObject>& layout, const QVector<WarehouseObject>& corridors) const {
    double energy = 0.0;

    for (int i = 0; i < layout.size(); ++i) {
        const auto& obj = layout[i];
        if (obj.isStatic) continue; // Статику не штрафуем за положение

        // 1. Штраф за выход за стены (грубая проверка по центрам + половина габарита)
        double maxRadius = std::max(obj.w, obj.l) / 2.0;
        if (obj.x - maxRadius < m_walls.left) energy += 1000;
        if (obj.x + maxRadius > m_walls.right) energy += 1000;
        if (obj.y - maxRadius < m_walls.top) energy += 1000;
        if (obj.y + maxRadius > m_walls.bottom) energy += 1000;

        // 2. Коллизии объектов друг с другом
        for (int j = i + 1; j < layout.size(); ++j) {
            if (isOverlapping(obj, layout[j])) energy += 500;
        }

        // 3. Коллизии с путями роботов
        for (const auto& corridor : corridors) {
            if (isOverlapping(obj, corridor)) energy += 800;
        }
    }
    return energy;
}

// ---------------- АЛГОРИТМ ОТЖИГА ----------------

void LayoutOptimizer::runAsyncOptimization(double maxRobotWidth) {
    QThreadPool::globalInstance()->start([this, maxRobotWidth]() {
        this->startOptimization(maxRobotWidth);
    });
}

void LayoutOptimizer::startOptimization(double maxRobotWidth) {
    qDebug() << "🚀 Starting Simulated Annealing...";

    // 1. Строим виртуальные коридоры для роботов
    QVector<WarehouseObject> corridors;
    for (const auto& edge : m_edges) {
        corridors << edge.getCorridorOBB(m_nodes, maxRobotWidth);
    }

    // 2. Параметры отжига
    double T = 1000.0;
    double T_min = 0.1;
    double alpha = 0.99; // Скорость остывания
    int iterationsPerTemp = 100; // Сколько попыток делаем на одной температуре

    double currentEnergy = calculateEnergy(m_layout, corridors);
    double initialEnergy = currentEnergy;

    // Вспомогательный генератор случайных чисел
    auto* rng = QRandomGenerator::global();

    // Главный цикл
    while (T > T_min) {
        for (int i = 0; i < iterationsPerTemp; ++i) {
            if (m_layout.isEmpty()) break;

            QVector<WarehouseObject> nextLayout = m_layout;

            // Выбираем случайный объект
            int idx = rng->bounded(nextLayout.size());
            if (nextLayout[idx].isStatic) continue;

            // Мутация: сдвигаем на небольшое случайное расстояние (до 0.5 метра)
            double shiftX = (rng->generateDouble() * 1.0) - 0.5;
            double shiftY = (rng->generateDouble() * 1.0) - 0.5;
            nextLayout[idx].x += shiftX;
            nextLayout[idx].y += shiftY;

            // С вероятностью 10% крутим на 90 градусов (удобно для стеллажей)
            if (rng->bounded(100) < 10) {
                nextLayout[idx].angle += 90.0;
                if (nextLayout[idx].angle >= 360.0) nextLayout[idx].angle -= 360.0;
            }

            // Считаем новую энергию
            double nextEnergy = calculateEnergy(nextLayout, corridors);
            double dE = nextEnergy - currentEnergy;

            // Принимаем ли мы новое состояние?
            if (dE < 0 || (std::exp(-dE / T) > rng->generateDouble())) {
                m_layout = nextLayout;
                currentEnergy = nextEnergy;
            }
        }

        T *= alpha; // Охлаждаем

        // (Опционально) Эмиттим прогресс
        // int percent = 100 - (T / 1000.0) * 100;
        // emit progressUpdated(percent);
    }

    qDebug() << "🏁 Optimization finished. Energy dropped from" << initialEnergy << "to" << currentEnergy;

    // 3. Отправляем результат обратно
    emit optimizationFinished(packLayoutToVariant());
}

// ---------------- УПАКОВКА ОБРАТНО В JSON/QML ----------------

QVariantList LayoutOptimizer::packLayoutToVariant() const {
    QVariantList list;
    for (const auto& obj : m_layout) {
        QVariantMap map;
        map["InstanceID"] = obj.instanceId;
        // Возвращаем обновленные координаты
        map["CoordX"] = obj.x;
        map["CoordY"] = obj.y;
        map["AngleRotation"] = obj.angle;
        // Остальные поля можно не возвращать, если QML умеет обновлять объекты по ID,
        // но лучше вернуть базовые для совместимости
        map["Width"] = obj.w;
        map["Length"] = obj.l;
        map["Type"] = obj.type;
        list.append(map);
    }
    return list;
}