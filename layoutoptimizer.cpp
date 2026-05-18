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
        n.initialX = n.x;
        n.initialY = n.y;
        n.type = nodeMap["MarkerType"].toString();
        m_nodes.append(n);
    }

    m_edges.clear();
    for (const QVariant& edgeVar : edges) {
        QVariantMap edgeMap = edgeVar.toMap();
        PathEdge e;
        e.id = edgeMap["EdgeID"].toInt();
        e.startNodeId = edgeMap["StartNodeID"].toInt();
        e.endNodeId = edgeMap["EndNodeID"].toInt();
        e.twoWayTraffic = edgeMap["TwoWayTraffic"].toBool();
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
        obj.initialX = obj.x;
        obj.initialY = obj.y;
        obj.initialAngle = obj.angle;
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

bool LayoutOptimizer::isPointInsideOBB(double px, double py, const WarehouseObject& obj) const {
    double rad = -obj.angle * M_PI / 180.0;
    double cosA = std::cos(rad);
    double sinA = std::sin(rad);

    // Переводим точку в локальную систему координат объекта
    double dx = px - obj.x;
    double dy = py - obj.y;

    double localX = dx * cosA - dy * sinA;
    double localY = dx * sinA + dy * cosA;

    // В локальной СК объект центрирован в (0,0) и не повернут
    // Добавляем небольшой буфер (0.5м) чтобы узлы не были "впритык" к объектам или в углах
    double hw = obj.w / 2.0 + 0.5;
    double hl = obj.l / 2.0 + 0.5;

    return (std::abs(localX) <= hw && std::abs(localY) <= hl);
}

// ---------------- РАСЧЕТ ЭНЕРГИИ ----------------

double LayoutOptimizer::pointToSegmentDistance(double px, double py, double x1, double y1, double x2, double y2) const {
    double dx = x2 - x1;
    double dy = y2 - y1;
    if (dx == 0 && dy == 0) {
        return std::sqrt((px - x1)*(px - x1) + (py - y1)*(py - y1));
    }
    double t = ((px - x1) * dx + (py - y1) * dy) / (dx * dx + dy * dy);
    t = std::max(0.0, std::min(1.0, t));
    double closestX = x1 + t * dx;
    double closestY = y1 + t * dy;
    return std::sqrt((px - closestX)*(px - closestX) + (py - closestY)*(py - closestY));
}

double LayoutOptimizer::calculateEnergy(const QVector<WarehouseObject>& layout, const QVector<PathNode>& nodes, const QVector<WarehouseObject>& corridors, double maxRobotWidth) const {
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
        // И проверка доступности пути (расстояние должно быть примерно равно maxRobotWidth/2)
        double minDistanceToPath = std::numeric_limits<double>::max();
        for (const auto& edge : m_edges) {
            const PathNode* n1 = nullptr;
            const PathNode* n2 = nullptr;
            for (const auto& n : nodes) {
                if (n.id == edge.startNodeId) n1 = &n;
                if (n.id == edge.endNodeId) n2 = &n;
            }
            if (n1 && n2) {
                double dist = pointToSegmentDistance(obj.x, obj.y, n1->x, n1->y, n2->x, n2->y);
                if (dist < minDistanceToPath) {
                    minDistanceToPath = dist;
                }
            }
        }

        for (const auto& corridor : corridors) {
            double overlap = getOverlapDistance(obj, corridor);
            if (overlap > 0) {
                energy += 8000 + overlap * 3000;
            }
        }

        // Штраф, если путь слишком далеко или слишком близко от объекта
        double targetDistance = std::max(obj.w, obj.l) / 2.0 + maxRobotWidth / 2.0;
        if (minDistanceToPath > targetDistance + 1.0) {
            energy += 200 * (minDistanceToPath - targetDistance); // Слишком далеко
        }

        // 4. Штраф за сильное отклонение от начальной позиции (сохраняем первоначальный замысел)
        double devX = obj.x - obj.initialX;
        double devY = obj.y - obj.initialY;
        double deviation = std::sqrt(devX*devX + devY*devY);
        if (deviation > 0.5) {
            energy += deviation * 50; // Мягкий штраф за отклонение
        }
    }

    // 5. Штраф за отклонение узлов путей от их начальных позиций (кроме start)
    for (const auto& node : nodes) {
        if (node.type != "start") {
            double devX = node.x - node.initialX;
            double devY = node.y - node.initialY;
            double deviation = std::sqrt(devX*devX + devY*devY);
            if (deviation > 0.5) {
                energy += deviation * 20; // Мягкий штраф
            }
        }

        // 6. Штраф, если узел находится внутри или слишком близко к объектам
        for (const auto& obj : layout) {
            if (isPointInsideOBB(node.x, node.y, obj)) {
                energy += 10000; // Очень высокий штраф, узлы не должны быть внутри или впритык к объектам
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
    // T_initial снижена, чтобы алгоритм не разрушал изначальную расстановку полностью.
    double T_initial = 200.0;
    double T = T_initial;
    double T_min = 0.1;
    double alpha = 0.99; // Коэффициент охлаждения
    int iterationsPerTemp = 100;

    double currentEnergy = calculateEnergy(m_layout, m_nodes, corridors, maxRobotWidth);
    double initialEnergy = currentEnergy;

    auto* rng = QRandomGenerator::global();

    // Главный цикл охлаждения
    while (T > T_min) {
        for (int i = 0; i < iterationsPerTemp; ++i) {
            QVector<WarehouseObject> nextLayout = m_layout;
            QVector<PathNode> nextNodes = m_nodes;
            QVector<PathEdge> nextEdges = m_edges;

            // Выбираем, что мутируем: объект или узел пути (с вероятностью 25% мутируем узел)
            int mutationCategory = rng->bounded(100);
            bool mutateNode = (mutationCategory < 25) && !nextNodes.isEmpty();
            bool deletedNode = false;

            if (mutateNode) {
                int nodeMutType = rng->bounded(100);

                if (nodeMutType < 5 && nextNodes.size() > 2) {
                    // 5% Удаление узла пути (если это не старт)
                    int idx = rng->bounded(nextNodes.size());
                    if (nextNodes[idx].type != "start") {
                        int nodeIdToRemove = nextNodes[idx].id;
                        nextNodes.removeAt(idx);

                        // Удаляем ребра, связанные с этим узлом
                        for (int j = nextEdges.size() - 1; j >= 0; --j) {
                            if (nextEdges[j].startNodeId == nodeIdToRemove || nextEdges[j].endNodeId == nodeIdToRemove) {
                                nextEdges.removeAt(j);
                            }
                        }
                        deletedNode = true;
                    }
                } else {
                    // 95% Сдвиг узла
                    int idx = rng->bounded(nextNodes.size());
                    if (nextNodes[idx].type != "start") {
                        double shiftRange = std::max(0.1, 0.5 * (T / T_initial));
                        nextNodes[idx].x += (rng->generateDouble() * 2.0 * shiftRange) - shiftRange;
                        nextNodes[idx].y += (rng->generateDouble() * 2.0 * shiftRange) - shiftRange;
                    }
                }
            } else if (!nextLayout.isEmpty()) {
                int idx = rng->bounded(nextLayout.size());
                if (nextLayout[idx].isStatic) continue;

                int mutationType = rng->bounded(100);

                if (mutationType < 20) {
                    // 20% Вращение ровно на 90 градусов (по просьбе пользователя)
                    nextLayout[idx].angle += 90.0;
                    if (nextLayout[idx].angle >= 360.0) nextLayout[idx].angle -= 360.0;
                } else {
                    // 80% Небольшой сдвиг
                    double shiftRange = std::max(0.1, 0.5 * (T / T_initial)); // Максимум 0.5м сдвига
                    double shiftX = (rng->generateDouble() * 2.0 * shiftRange) - shiftRange;
                    double shiftY = (rng->generateDouble() * 2.0 * shiftRange) - shiftRange;
                    nextLayout[idx].x += shiftX;
                    nextLayout[idx].y += shiftY;
                }
            }

            // При мутации узлов нужно перестроить коридоры
            QVector<WarehouseObject> nextCorridors = corridors;
            if (mutateNode) {
                nextCorridors.clear();
                for (const auto& edge : nextEdges) {
                    nextCorridors << edge.getCorridorOBB(nextNodes, maxRobotWidth);
                }
            }

            // Вычисляем энергию нового состояния
            double nextEnergy = calculateEnergy(nextLayout, nextNodes, nextCorridors, maxRobotWidth);

            // Добавляем поощрение за меньшее количество узлов (чтобы отжиг стремился удалять лишние)
            if (deletedNode) {
                 nextEnergy -= 1000;
            }

            double dE = nextEnergy - currentEnergy;

            // Критерий Метрополиса:
            // 1. Всегда принимаем улучшение (dE < 0)
            // 2. Если ухудшение (dE >= 0), принимаем его с определенной вероятностью,
            //    которая зависит от температуры T. При высокой T вероятность велика.
            if (dE < 0 || (std::exp(-dE / T) > rng->generateDouble())) {
                m_layout = nextLayout;
                m_nodes = nextNodes;
                if (mutateNode) {
                    m_edges = nextEdges;
                    corridors = nextCorridors;
                }
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
    emit optimizationFinished(packLayoutToVariant(), packNodesToVariant(), packEdgesToVariant());
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

QVariantList LayoutOptimizer::packEdgesToVariant() const {
    QVariantList list;
    for (const auto& edge : m_edges) {
        QVariantMap map;
        map["EdgeID"] = edge.id;
        map["StartNodeID"] = edge.startNodeId;
        map["EndNodeID"] = edge.endNodeId;
        map["TwoWayTraffic"] = edge.twoWayTraffic;
        list.append(map);
    }
    return list;
}

QVariantList LayoutOptimizer::packNodesToVariant() const {
    QVariantList list;
    for (const auto& node : m_nodes) {
        QVariantMap map;
        map["NodeID"] = node.id;
        map["NodeX"] = node.x;
        map["NodeY"] = node.y;
        map["MarkerType"] = node.type;
        list.append(map);
    }
    return list;
}