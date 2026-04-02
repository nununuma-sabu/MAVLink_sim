#ifndef MAPVIEW3D_H
#define MAPVIEW3D_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QVector3D>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QVector>
#include <QTimer>

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

    /// ドローン位置・姿勢を更新
    void updateDrone(double latitude, double longitude, double altitude,
                     double roll, double pitch, double yaw);

    /// ホームポジション設定
    void setHome(double latitude, double longitude);

    /// 飛行経路をクリア
    void clearTrace();

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
    void drawGrid();
    void drawAxes();
    void drawDrone();
    void drawTrace();
    void drawShadow();
    void drawAltitudeLine();
    void drawHomeMarker();
    void drawHUD();

    QVector3D geoToLocal(double lat, double lon, double alt) const;

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
    double m_homeLat = 35.6812;
    double m_homeLon = 139.7671;
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
};

#endif // MAPVIEW3D_H
