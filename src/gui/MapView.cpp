#include "MapView.h"
#include "core/MissionManager.h"
#include <QWheelEvent>
#include <QMouseEvent>
#include <QtMath>
#include <QGraphicsTextItem>

MapView::MapView(QWidget *parent)
    : QGraphicsView(parent)
{
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);
    setupScene();

    setRenderHint(QPainter::Antialiasing);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    setBackgroundBrush(QColor(25, 25, 30));
    setMinimumSize(300, 300);
}

void MapView::setupScene()
{
    m_scene->setSceneRect(-500, -500, 1000, 1000);

    drawGrid();

    // ホームマーカー
    m_homeItem = m_scene->addEllipse(-6, -6, 12, 12,
                                      QPen(QColor(46, 204, 113), 2),
                                      QBrush(QColor(46, 204, 113, 100)));
    m_homeItem->setZValue(5);

    // 飛行経路
    m_traceItem = m_scene->addPath(QPainterPath(),
                                    QPen(QColor(52, 152, 219, 180), 2));
    m_traceItem->setZValue(3);

    // ドローンアイコン（三角形）
    QPolygonF dronePoly;
    dronePoly << QPointF(0, -10)
              << QPointF(-7, 7)
              << QPointF(0, 4)
              << QPointF(7, 7);
    m_droneItem = m_scene->addPolygon(dronePoly,
                                       QPen(QColor(255, 200, 0), 1.5),
                                       QBrush(QColor(255, 200, 0, 200)));
    m_droneItem->setZValue(10);
    m_droneItem->setTransformOriginPoint(0, 0);

    // ウェイポイント経路ライン
    m_wpPathItem = m_scene->addPath(QPainterPath(),
        QPen(QColor(231, 76, 60, 150), 1.5, Qt::DashLine));
    m_wpPathItem->setZValue(4);

    // 初期ズーム
    scale(1.5, 1.5);
}

void MapView::drawGrid()
{
    QPen gridPen(QColor(50, 50, 60), 0.5);
    QPen majorGridPen(QColor(70, 70, 80), 1.0);

    // グリッド (10m間隔 = 50px間隔)
    double gridSpacing = m_scale * 10.0; // 10m
    double majorSpacing = m_scale * 50.0; // 50m

    for (double x = -500; x <= 500; x += gridSpacing) {
        bool major = (fmod(qAbs(x), majorSpacing) < 0.01);
        m_scene->addLine(x, -500, x, 500, major ? majorGridPen : gridPen)->setZValue(0);
    }
    for (double y = -500; y <= 500; y += gridSpacing) {
        bool major = (fmod(qAbs(y), majorSpacing) < 0.01);
        m_scene->addLine(-500, y, 500, y, major ? majorGridPen : gridPen)->setZValue(0);
    }

    // 座標軸
    m_scene->addLine(0, -500, 0, 500, QPen(QColor(100, 100, 120), 1))->setZValue(1);
    m_scene->addLine(-500, 0, 500, 0, QPen(QColor(100, 100, 120), 1))->setZValue(1);

    // N/S/E/Wラベル
    auto addLabel = [this](const QString &text, double x, double y) {
        auto *item = m_scene->addText(text);
        item->setDefaultTextColor(QColor(150, 150, 160));
        item->setPos(x, y);
        item->setZValue(2);
    };
    addLabel("N", -5, -500);
    addLabel("S", -5, 480);
    addLabel("E", 480, -8);
    addLabel("W", -500, -8);
}

QPointF MapView::geoToScene(double lat, double lon) const
{
    // ホーム位置からのオフセット (メートル) → シーン座標
    double dlat = (lat - m_homeLat) * 111320.0;
    double dlon = (lon - m_homeLon) * 111320.0 * qCos(qDegreesToRadians(m_homeLat));

    // NED → シーン座標 (Y軸反転: 北が上)
    return QPointF(dlon * m_scale, -dlat * m_scale);
}

void MapView::setHome(double latitude, double longitude)
{
    m_homeLat = latitude;
    m_homeLon = longitude;
    m_homeSet = true;
    m_homeItem->setPos(0, 0);
}

void MapView::updatePosition(double latitude, double longitude, double yaw)
{
    if (!m_homeSet) {
        setHome(latitude, longitude);
    }

    QPointF pos = geoToScene(latitude, longitude);
    m_droneItem->setPos(pos);
    m_droneItem->setRotation(qRadiansToDegrees(yaw));

    // 飛行経路追加
    if (!m_traceStarted) {
        m_tracePath.moveTo(pos);
        m_traceStarted = true;
    } else {
        m_tracePath.lineTo(pos);
    }
    m_traceItem->setPath(m_tracePath);
}

void MapView::clearTrace()
{
    m_tracePath = QPainterPath();
    m_traceStarted = false;
    m_traceItem->setPath(m_tracePath);
}

void MapView::wheelEvent(QWheelEvent *event)
{
    double factor = 1.15;
    if (event->angleDelta().y() < 0) {
        factor = 1.0 / factor;
    }
    scale(factor, factor);
}

void MapView::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
}

void MapView::mousePressEvent(QMouseEvent *event)
{
    // Ctrl+クリックでウェイポイント追加
    if (event->modifiers() & Qt::ControlModifier &&
        event->button() == Qt::LeftButton) {
        QPointF scenePos = mapToScene(event->pos());
        // シーン座標 → 地理座標に変換
        double dlon = scenePos.x() / m_scale;
        double dlat = -scenePos.y() / m_scale;
        double lat = m_homeLat + dlat / 111320.0;
        double lon = m_homeLon + dlon / (111320.0 * qCos(qDegreesToRadians(m_homeLat)));
        emit waypointAddRequested(lat, lon);
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

// ============================================================
// ウェイポイント表示
// ============================================================

void MapView::setWaypoints(const QVector<MissionItem> &items)
{
    clearWaypoints();

    QPainterPath wpPath;

    for (int i = 0; i < items.size(); i++) {
        const auto &wp = items[i];
        QPointF pos = geoToScene(wp.latitude, wp.longitude);

        // マーカー（円）
        double r = 6.0;
        QColor markerColor = (i == m_activeWpIndex)
            ? QColor(255, 200, 0)     // アクティブ: 黄
            : QColor(231, 76, 60);    // 通常: 赤

        auto *marker = m_scene->addEllipse(
            -r, -r, r * 2, r * 2,
            QPen(markerColor, 2),
            QBrush(markerColor.lighter(150)));
        marker->setPos(pos);
        marker->setZValue(7);
        m_wpMarkers.append(marker);

        // 番号ラベル
        auto *label = m_scene->addText(QString::number(i + 1));
        label->setDefaultTextColor(Qt::white);
        label->setPos(pos.x() + r + 2, pos.y() - r - 2);
        label->setZValue(8);
        QFont f = label->font();
        f.setPixelSize(10);
        f.setBold(true);
        label->setFont(f);
        m_wpLabels.append(label);

        // 経路ライン
        if (i == 0) {
            wpPath.moveTo(pos);
        } else {
            wpPath.lineTo(pos);
        }
    }

    m_wpPathItem->setPath(wpPath);
}

void MapView::setActiveWaypoint(int index)
{
    m_activeWpIndex = index;

    // マーカーの色を更新
    for (int i = 0; i < m_wpMarkers.size(); i++) {
        QColor color = (i == index)
            ? QColor(255, 200, 0)
            : QColor(231, 76, 60);
        m_wpMarkers[i]->setPen(QPen(color, 2));
        m_wpMarkers[i]->setBrush(QBrush(color.lighter(150)));
    }
}

void MapView::clearWaypoints()
{
    for (auto *marker : m_wpMarkers) {
        m_scene->removeItem(marker);
        delete marker;
    }
    m_wpMarkers.clear();

    for (auto *label : m_wpLabels) {
        m_scene->removeItem(label);
        delete label;
    }
    m_wpLabels.clear();

    m_wpPathItem->setPath(QPainterPath());
}
