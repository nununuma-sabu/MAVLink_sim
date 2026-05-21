#ifndef MAPVIEW3D_H
#define MAPVIEW3D_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QVector3D>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QVector>
#include <QTimer>
#include "core/GeoUtils.h"
#include "BuildingProvider.h"
#include "Map3DMesh.h"

struct MissionItem;

/**
 * @brief 3Dドローンマップビュー
 *
 * QOpenGLWidgetベースのドローン3D表示。
 * グリッド地面、ドローンモデル、飛行経路を3Dで描画。
 * マウスでカメラ操作（回転/ズーム/パン）。
 */
class MapView3D : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    explicit MapView3D(QWidget *parent = nullptr);
    ~MapView3D() override;

    /// ドローン位置・姿勢を更新
    void updateDrone(double latitude, double longitude, double altitude,
                     double roll, double pitch, double yaw);

    /// ホームポジション設定
    void setHome(double latitude, double longitude, int radiusMeters = 300,
                 const QString &locationName = QString(),
                 float cameraDistance = 95.0f,
                 float cameraAngleX = 42.0f,
                 float cameraAngleY = -45.0f);

    /// 飛行経路をクリア
    void clearTrace();

    /// ウェイポイント表示を更新
    void setWaypoints(const QVector<MissionItem> &items);
    /// アクティブWPをハイライト
    void setActiveWaypoint(int index);

    QSize minimumSizeHint() const override { return QSize(400, 300); }

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void drawSkyGradient();
    void drawAxes();
    void drawDrone();
    void drawTrace();
    void drawShadow();
    void drawAltitudeLine();
    void drawHomeMarker();
    void drawHUD();
    void drawWaypoints();
    void drawStaticCityMesh();
    void drawBuildingLabels(QPainter &painter);

    QVector3D geoToLocal(double lat, double lon, double alt) const;
    QVector3D buildingPointToLocal(const QVector2D &point, float altitude) const;
    QVector3D buildingCenter(const BuildingData &building) const;
    bool worldToScreen(const QVector3D &world, QPointF &screen) const;
    void rebuildStaticCityMesh();
    void uploadStaticCityMeshToGpu();
    void clearStaticCityVbos();
    void uploadTraceToGpu();
    void uploadWaypointPathToGpu();
    void uploadWaypointMarkersToGpu();
    void uploadDroneModelToGpu();
    void clearDynamicVbos();
    void drawDynamicVbo(GLuint buffer, int vertexCount, GLenum primitive, float lineWidth);
    bool initializeStaticCityShader();
    QMatrix4x4 projectionMatrix() const;
    QMatrix4x4 viewMatrix() const;

    struct StaticVboBatch {
        Map3DPrimitive primitive = Map3DPrimitive::Triangles;
        GLuint vertexBuffer = 0;
        int vertexCount = 0;
        float lineWidth = 1.0f;
    };

    // カメラパラメータ
    float m_cameraDistance = 40.0f;
    float m_cameraAngleX  = 30.0f;   // 上下角度 (deg)
    float m_cameraAngleY  = -45.0f;  // 左右角度 (deg)
    QVector3D m_cameraTarget = {0, 0, 0};

    // マウス操作
    QPoint m_lastMousePos;
    bool m_rotating = false;
    bool m_panning  = false;

    // ホーム位置
    double m_homeLat = Geo::NerimaStationLat;
    double m_homeLon = Geo::NerimaStationLon;
    bool m_homeSet = false;

    // ドローン状態
    QVector3D m_dronePos = {0, 0, 0};
    float m_droneRoll  = 0;
    float m_dronePitch = 0;
    float m_droneYaw   = 0;

    // 飛行経路
    QVector<QVector3D> m_tracePath;
    static constexpr int MAX_TRACE_POINTS = 5000;

    // アニメーション
    float m_propellerAngle = 0.0f;
    QTimer m_animTimer;

    // ウェイポイント
    struct WpData {
        QVector3D pos;
        uint16_t command;
        int seq;
    };
    QVector<WpData> m_waypoints;
    int m_activeWpIndex = -1;

    // 建物
    QVector<BuildingData> m_buildings;
    QVector<GroundPathData> m_groundPaths;
    Map3DStaticMesh m_staticCityMesh;
    QVector<StaticVboBatch> m_staticCityVboBatches;
    GLuint m_traceVbo = 0;
    int m_traceVboCount = 0;
    bool m_traceVboDirty = true;
    GLuint m_waypointPathVbo = 0;
    int m_waypointPathVboCount = 0;
    bool m_waypointPathVboDirty = true;
    GLuint m_waypointMarkerTriangleVbo = 0;
    int m_waypointMarkerTriangleVboCount = 0;
    GLuint m_waypointMarkerLineVbo = 0;
    int m_waypointMarkerLineVboCount = 0;
    bool m_waypointMarkerVboDirty = true;
    GLuint m_droneModelVbo = 0;
    int m_droneModelVboCount = 0;
    bool m_droneModelVboDirty = true;
    QOpenGLShaderProgram *m_staticCityProgram = nullptr;
    int m_staticCityMvpLocation = -1;
    int m_staticCityModelViewLocation = -1;
    int m_staticCityFogColorLocation = -1;
    int m_staticCityFogStartLocation = -1;
    int m_staticCityFogEndLocation = -1;
    int m_staticCityPositionLocation = -1;
    int m_staticCityColorLocation = -1;
    bool m_glInitialized = false;
    bool m_staticCityVboDirty = true;
    BuildingProvider *m_buildingProvider = nullptr;
    QString m_locationName = "練馬駅";
    QString m_buildingStatus = "建物データ: 未読み込み";
};

#endif // MAPVIEW3D_H
