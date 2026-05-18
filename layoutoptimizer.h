#ifndef LAYOUTOPTIMIZER_H
#define LAYOUTOPTIMIZER_H

#include <QObject>
#include <QVector>
#include <QPointF>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <cmath>

class DatabaseManager; // Предварительное объявление

// ---------------- СТРУКТУРЫ ДАННЫХ ----------------

struct WarehouseObject
{
    int instanceId;
    QString modelId;
    double x, y;
    double initialX = 0, initialY = 0;
    double initialAngle = 0;
    double w, l;
    double angle;
    QString type;
    bool isStatic = false;

    QVector<QPointF> getVertices() const
    {
        QVector<QPointF> v;
        double rad = angle * M_PI / 180.0;
        double cosA = std::cos(rad);
        double sinA = std::sin(rad);

        auto rotate = [&](double px, double py) {
            return QPointF(x + px * cosA - py * sinA, y + px * sinA + py * cosA);
        };

        double hw = w / 2.0;
        double hl = l / 2.0;

        v << rotate(-hw, -hl) << rotate(hw, -hl) << rotate(hw, hl) << rotate(-hw, hl);
        return v;
    }
};

struct PathNode
{
    int id;
    double x, y;
    double initialX = 0, initialY = 0;
    QString type; // "start", "path", etc.
};

struct PathEdge
{
    int id;
    int startNodeId;
    int endNodeId;
    bool twoWayTraffic = false;

    WarehouseObject getCorridorOBB(const QVector<PathNode> &nodes, double robotWidth) const
    {
        const PathNode *n1 = nullptr;
        const PathNode *n2 = nullptr;
        for (const auto &n : nodes) {
            if (n.id == startNodeId)
                n1 = &n;
            if (n.id == endNodeId)
                n2 = &n;
        }
        if (!n1 || !n2)
            return {};

        double dx = n2->x - n1->x;
        double dy = n2->y - n1->y;
        double length = std::sqrt(dx * dx + dy * dy);
        double angle = std::atan2(dy, dx) * 180.0 / M_PI;

        WarehouseObject corridor;
        corridor.instanceId = id;
        corridor.modelId = "";
        corridor.x = (n1->x + n2->x) / 2.0;
        corridor.y = (n1->y + n2->y) / 2.0;
        corridor.w = length;
        corridor.l = robotWidth;
        corridor.angle = angle;
        corridor.type = "corridor";
        corridor.isStatic = true;

        return corridor;
    }
};

struct WallConstraints
{
    double top, bottom, left, right;
};

// ---------------- КЛАСС ОПТИМИЗАТОРА ----------------

class LayoutOptimizer : public QObject
{
    Q_OBJECT
public:
    explicit LayoutOptimizer(QObject *parent = nullptr);

public slots:
    // Установка статических данных (стены и пути)
    Q_INVOKABLE void setEnvironment(const QVariantMap &walls,
                                    const QVariantList &nodes,
                                    const QVariantList &edges);

    // Подготовка (обогащение) данных из БД. ВАЖНО: вызывать в основном потоке!
    Q_INVOKABLE void prepareLayout(const QVariantList &rawLayout, QObject *dbManagerObj);

    // Главный метод запуска отжига
    Q_INVOKABLE void startOptimization(double maxRobotWidth);
    Q_INVOKABLE void runAsyncOptimization(double maxRobotWidth);

signals:
    // Сигналы для общения с QML и основным потоком
    void optimizationFinished(QVariantList updatedLayout, QVariantList updatedNodes, QVariantList updatedEdges);
    void progressUpdated(int percent);

private:
    // Внутренние данные
    QVector<WarehouseObject> m_layout;
    QVector<PathNode> m_nodes;
    QVector<PathEdge> m_edges;
    WallConstraints m_walls;

    // Математика SAT (Детектор столкновений)
    void getAxes(const QVector<QPointF>& v, QVector<QPointF>& axes) const;
    void project(const QVector<QPointF>& v, const QPointF& axis, double& min, double& max) const;
    bool isOverlapping(const WarehouseObject& a, const WarehouseObject& b) const;
    double getOverlapDistance(const WarehouseObject& a, const WarehouseObject& b) const;
    bool isPointInsideOBB(double px, double py, const WarehouseObject& obj) const;

    // Расчет энергии
    double calculateEnergy(const QVector<WarehouseObject>& layout, const QVector<PathNode>& nodes, const QVector<WarehouseObject>& corridors, double maxRobotWidth) const;
    double pointToSegmentDistance(double px, double py, double x1, double y1, double x2, double y2) const;

    // Сборка ответа для QML
    QVariantList packLayoutToVariant() const;
    QVariantList packNodesToVariant() const;
    QVariantList packEdgesToVariant() const;
};

#endif // LAYOUTOPTIMIZER_H