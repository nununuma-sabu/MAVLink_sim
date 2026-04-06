#ifndef MAPVIEW_H
#define MAPVIEW_H

#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QGraphicsTextItem>
#include <QVector>
#include <QPointF>

struct MissionItem;

/**
 * @brief 2Dトップダウンマップビュー
 *
 * QGraphicsSceneベースのドローン位置表示。
 * グリッド、飛行経路（トレース）、ドローンアイコンを表示。
 */
class MapView : public QGraphicsView
{
    Q_OBJECT

public:
    explicit MapView(QWidget *parent = nullptr);

    /// ドローン位置を更新
    void updatePosition(double latitude, double longitude, double yaw);

    /// ホームポジション設定
    void setHome(double latitude, double longitude);

    /// トレースをクリア
    void clearTrace();

    /// ウェイポイント表示を更新
    void setWaypoints(const QVector<MissionItem> &items);
    /// アクティブWPをハイライト
    void setActiveWaypoint(int index);
    /// ウェイポイント表示をクリア
    void clearWaypoints();

signals:
    /// マップクリックでWP追加要求
    void waypointAddRequested(double lat, double lon);

protected:
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    void setupScene();
    void drawGrid();
    QPointF geoToScene(double lat, double lon) const;

    QGraphicsScene *m_scene;

    // ドローンアイテム
    QGraphicsPolygonItem *m_droneItem = nullptr;
    QGraphicsPathItem *m_traceItem = nullptr;

    // ホームアイテム
    QGraphicsEllipseItem *m_homeItem = nullptr;

    // ホーム位置
    double m_homeLat = 35.6812;
    double m_homeLon = 139.7671;
    bool m_homeSet = false;

    // トレース
    QPainterPath m_tracePath;
    bool m_traceStarted = false;

    // スケール (ピクセル/メートル)
    double m_scale = 5.0;

    // ウェイポイント表示
    QVector<QGraphicsEllipseItem*> m_wpMarkers;
    QVector<QGraphicsTextItem*> m_wpLabels;
    QGraphicsPathItem *m_wpPathItem = nullptr;
    int m_activeWpIndex = -1;
};

#endif // MAPVIEW_H
