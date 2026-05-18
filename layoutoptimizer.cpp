#include "layoutoptimizer.h"
#include "databasemanager.h" // Твой класс БД
#include <limits>
#include <QDebug>
#include <QRandomGenerator>
#include <QtConcurrent/QtConcurrent>
#include <QThreadPool>

LayoutOptimizer::LayoutOptimizer(QObject *parent) : QObject(parent) {}

void LayoutOptimizer::setEnvironment(const QVariantMap& walls, const QVariantList& nodes, const QVariantList& edges) {
    m_walls.top = walls["top"].toDouble();
    m_walls.bottom = walls["bottom"].toDouble();
    m_walls.left = walls["left"].toDouble();
    m_walls.right = walls["right"].toDouble();

    m_nodes.clear();
    for (const QVariant& nodeVar : nodes) {
        QVariantMap nodeMap = nodeVar.toMap();
        PathNode n;
        n.id = nodeMap["NodeID"].toInt();
        n.x = nodeMap["NodeX"].toDouble();
        n.y = nodeMap["NodeY"].toDouble();
        m_nodes.append(n);
    }

    m_edges.clear();
    for (const QVariant& edgeVar : edges) {
        QVariantMap edgeMap = edgeVar.toMap();
        PathEdge e;
        e.id = edgeMap["EdgeID"].toInt();
        e.startNodeId = edgeMap["StartNodeID"].toInt();
        e.endNodeId = edgeMap["EndNodeID"].toInt();
        m_edges.append(e);
    }
}

// ---------------- ОБОГАЩЕНИЕ ДАННЫХ ----------------

void LayoutOptimizer::prepareLayout(const QVariantList &rawLayout, QObject *dbManagerObj) {
    DatabaseManager* dbManager = qobject_cast<DatabaseManager*>(dbManagerObj);
    if (!dbManager) {
        qDebug() << "❌ Error: dbManager is null or not a DatabaseManager in prepareLayout";
        return;
    }
    m_layout.clear();

    for (const QVariant &item : rawLayout) {
        QVariantMap objMap = item.toMap();
        QString modelId = objMap["ModelID"].toString();

        // Идем в БД за размерами
        QVariantMap dbData = dbManager->getEquipmentInfo(modelId);

        WarehouseObject obj;
        obj.instanceId = objMap["InstanceID"].toInt();
        obj.modelId = modelId;
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

void LayoutOptimizer::getAxes(const QVector<QPointF>& v, QVector<QPointF>& axes) const {
    for (int i = 0; i < v.size(); ++i) {
        QPointF p1 = v[i];
        QPointF p2 = v[(i + 1) % v.size()];
        QPointF edge = p2 - p1;
        axes << QPointF(-edge.y(), edge.x()); // Нормаль
    }
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

    QVector<QPointF> axes;
    axes.reserve(v1.size() + v2.size());
    getAxes(v1, axes);
    getAxes(v2, axes);

    for (const auto& axis : axes) {
        double min1, max1, min2, max2;
        project(v1, axis, min1, max1);
        project(v2, axis, min2, max2);

        if (max1 < min2 || max2 < min1) return false; // Найдена разделяющая ось
    }
    return true; // Пересекаются
}

double LayoutOptimizer::getOverlapDistance(const WarehouseObject& a, const WarehouseObject& b) const {
    QVector<QPointF> v1 = a.getVertices();
    QVector<QPointF> v2 = b.getVertices();

    QVector<QPointF> axes;
    axes.reserve(v1.size() + v2.size());
    getAxes(v1, axes);
    getAxes(v2, axes);

    double minOverlap = std::numeric_limits<double>::max();

    for (const auto& axis : axes) {
        double min1, max1, min2, max2;
        project(v1, axis, min1, max1);
        project(v2, axis, min2, max2);

        if (max1 < min2 || max2 < min1) return 0.0; // Разделяющая ось - не пересекаются

        double overlap = std::min(max1, max2) - std::max(min1, min2);
        if (overlap < minOverlap) {
            minOverlap = overlap;
        }
    }
    return minOverlap;
}

// ---------------- РАСЧЕТ ЭНЕРГИИ ----------------

double LayoutOptimizer::calculateEnergy(const QVector<WarehouseObject>& layout, const QVector<WarehouseObject>& corridors) const {
    double energy = 0.0;

    for (int i = 0; i < layout.size(); ++i) {
        const auto& obj = layout[i];
        if (obj.isStatic) continue; // Статику не штрафуем за положение (она неподвижна)

        // 1. Штраф за выход за стены. Учитываем точные вершины для более точного расчета
        QVector<QPointF> vertices = obj.getVertices();
        for (const auto& v : vertices) {
            if (v.x() < m_walls.left) energy += 1000 + (m_walls.left - v.x()) * 500;
            if (v.x() > m_walls.right) energy += 1000 + (v.x() - m_walls.right) * 500;
            if (v.y() < m_walls.top) energy += 1000 + (m_walls.top - v.y()) * 500;
            if (v.y() > m_walls.bottom) energy += 1000 + (v.y() - m_walls.bottom) * 500;
        }

        // 2. Коллизии объектов друг с другом и штраф за слишком близкое расположение
        for (int j = 0; j < layout.size(); ++j) {
            if (i == j) continue; // Не сравниваем с самим собой

            double overlap = getOverlapDistance(obj, layout[j]);
            if (overlap > 0) {
                // Жесткий штраф за пересечение, зависящий от глубины проникновения
                energy += 5000 + overlap * 2000;
            } else {
                // Мягкий штраф, если объекты слишком близко (минимальный зазор, скажем, 0.5 метра)
                // Для упрощения считаем расстояние между центрами минус половина габаритов
                double dx = obj.x - layout[j].x;
                double dy = obj.y - layout[j].y;
                double dist = std::sqrt(dx*dx + dy*dy);
                double minAllowedDist = std::max(obj.w, obj.l)/2.0 + std::max(layout[j].w, layout[j].l)/2.0 + 0.5;
                if (dist < minAllowedDist) {
                    energy += 100 * (minAllowedDist - dist);
                }
            }
        }

        // 3. Коллизии с путями роботов (чтобы объекты не перекрывали коридоры)
        for (const auto& corridor : corridors) {
            double overlap = getOverlapDistance(obj, corridor);
            if (overlap > 0) {
                energy += 8000 + overlap * 3000;
            }
        }
    }
    return energy;
}

// ---------------- АЛГОРИТМ ОТЖИГА ----------------

void LayoutOptimizer::runAsyncOptimization(double maxRobotWidth) {
    // Используем QtConcurrent для асинхронного запуска в другом потоке,
    // чтобы не блокировать интерфейс QML (Main thread)
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

    // 2. Параметры алгоритма имитации отжига (Simulated Annealing)
    // T (Температура) определяет вероятность принятия ухудшающих решений.
    // В начале (при высокой T) алгоритм "прыгает" по возможным состояниям, чтобы выйти из локальных минимумов.
    double T_initial = 1000.0;
    double T = T_initial;
    double T_min = 0.1;
    double alpha = 0.99; // Коэффициент охлаждения. Чем ближе к 1, тем медленнее и точнее поиск
    int iterationsPerTemp = 100; // Количество мутаций на одной температурной ступени

    double currentEnergy = calculateEnergy(m_layout, corridors);
    double initialEnergy = currentEnergy;

    auto* rng = QRandomGenerator::global();

    // Главный цикл охлаждения
    while (T > T_min) {
        for (int i = 0; i < iterationsPerTemp; ++i) {
            if (m_layout.isEmpty()) break;

            QVector<WarehouseObject> nextLayout = m_layout;

            // Выбираем случайный объект для мутации
            int idx = rng->bounded(nextLayout.size());
            if (nextLayout[idx].isStatic) continue;

            // Выбираем тип мутации с помощью случайного числа
            int mutationType = rng->bounded(100);

            if (mutationType < 5) {
                // 5% вероятность: Радикальное перемещение в пределах склада
                // Помогает, если объект "застрял" в плохом месте
                nextLayout[idx].x = m_walls.left + rng->generateDouble() * (m_walls.right - m_walls.left);
                nextLayout[idx].y = m_walls.top + rng->generateDouble() * (m_walls.bottom - m_walls.top);
            } else if (mutationType < 15) {
                // 10% вероятность: Произвольное вращение
                // Позволяет объектам "втиснуться" в неудобные места
                double angleShift = (rng->generateDouble() * 180.0) - 90.0; // от -90 до +90
                nextLayout[idx].angle += angleShift;
                if (nextLayout[idx].angle < 0.0) nextLayout[idx].angle += 360.0;
                if (nextLayout[idx].angle >= 360.0) nextLayout[idx].angle -= 360.0;
            } else if (mutationType < 25) {
                // 10% вероятность: Вращение ровно на 90 градусов
                // Для складов часто важны прямые углы
                nextLayout[idx].angle += 90.0;
                if (nextLayout[idx].angle >= 360.0) nextLayout[idx].angle -= 360.0;
            } else if (mutationType < 30) {
                // 5% вероятность: Обмен позициями двух объектов (swap)
                // Помогает быстро переставить объекты местами без штрафов за их пересечение по пути
                int idx2 = rng->bounded(nextLayout.size());
                if (!nextLayout[idx2].isStatic && idx != idx2) {
                    double tempX = nextLayout[idx].x;
                    double tempY = nextLayout[idx].y;
                    nextLayout[idx].x = nextLayout[idx2].x;
                    nextLayout[idx].y = nextLayout[idx2].y;
                    nextLayout[idx2].x = tempX;
                    nextLayout[idx2].y = tempY;
                }
            } else {
                // 70% вероятность: Небольшой сдвиг (локальная оптимизация)
                // Чем ниже температура, тем меньше должен быть максимальный сдвиг
                double shiftRange = std::max(0.1, 1.0 * (T / T_initial)); // От 1.0 до 0.1 м
                double shiftX = (rng->generateDouble() * 2.0 * shiftRange) - shiftRange;
                double shiftY = (rng->generateDouble() * 2.0 * shiftRange) - shiftRange;
                nextLayout[idx].x += shiftX;
                nextLayout[idx].y += shiftY;
            }

            // Вычисляем энергию нового состояния
            double nextEnergy = calculateEnergy(nextLayout, corridors);

            // Разница энергий (dE). Если dE < 0, значит новое состояние лучше (энергия упала).
            double dE = nextEnergy - currentEnergy;

            // Критерий Метрополиса:
            // 1. Всегда принимаем улучшение (dE < 0)
            // 2. Если ухудшение (dE >= 0), принимаем его с определенной вероятностью,
            //    которая зависит от температуры T. При высокой T вероятность велика.
            if (dE < 0 || (std::exp(-dE / T) > rng->generateDouble())) {
                m_layout = nextLayout;
                currentEnergy = nextEnergy;
            }
        }

        // Уменьшаем температуру (охлаждение)
        T *= alpha;

        // Отправляем прогресс в QML
        // Вычисляем логарифмический прогресс для более плавного движения полосы загрузки
        double totalSteps = std::log(T_min / T_initial) / std::log(alpha);
        double currentStep = std::log(T / T_initial) / std::log(alpha);
        int percent = static_cast<int>((currentStep / totalSteps) * 100);
        emit progressUpdated(percent);
    }

    qDebug() << "🏁 Optimization finished. Energy dropped from" << initialEnergy << "to" << currentEnergy;

    // 3. Отправляем результат обратно в QML
    emit optimizationFinished(packLayoutToVariant());
}

// ---------------- УПАКОВКА ОБРАТНО В JSON/QML ----------------

QVariantList LayoutOptimizer::packLayoutToVariant() const {
    QVariantList list;
    for (const auto& obj : m_layout) {
        QVariantMap map;
        map["InstanceID"] = obj.instanceId;
        map["ModelID"] = obj.modelId;
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