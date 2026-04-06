#include "MapView3D.h"
#include "core/MissionManager.h"
#include <QtMath>
#include <QPainter>
#include <QDebug>

MapView3D::MapView3D(QWidget *parent)
    : QOpenGLWidget(parent)
{
    setMinimumSize(400, 300);
    setFocusPolicy(Qt::StrongFocus);

    // プロペラアニメーション用タイマー
    connect(&m_animTimer, &QTimer::timeout, [this]() {
        m_propellerAngle += 15.0f;
        if (m_propellerAngle >= 360.0f) m_propellerAngle -= 360.0f;
        update();
    });
    m_animTimer.start(33); // ~30fps
}

QVector3D MapView3D::geoToLocal(double lat, double lon, double alt) const
{
    double dlat = (lat - m_homeLat) * 111320.0;
    double dlon = (lon - m_homeLon) * 111320.0 * qCos(qDegreesToRadians(m_homeLat));
    // OpenGL座標: X=East, Y=Up, Z=South (右手系)
    return QVector3D(static_cast<float>(dlon),
                     static_cast<float>(alt),
                     static_cast<float>(-dlat));
}

void MapView3D::setHome(double latitude, double longitude)
{
    m_homeLat = latitude;
    m_homeLon = longitude;
    m_homeSet = true;
}

void MapView3D::updateDrone(double latitude, double longitude, double altitude,
                             double roll, double pitch, double yaw)
{
    if (!m_homeSet) {
        setHome(latitude, longitude);
    }

    m_dronePos = geoToLocal(latitude, longitude, altitude);
    m_droneRoll = static_cast<float>(qRadiansToDegrees(roll));
    m_dronePitch = static_cast<float>(qRadiansToDegrees(pitch));
    m_droneYaw = static_cast<float>(qRadiansToDegrees(yaw));

    // トレース追加
    if (m_tracePath.isEmpty() ||
        (m_tracePath.last() - m_dronePos).length() > 0.1f) {
        m_tracePath.append(m_dronePos);
        if (m_tracePath.size() > MAX_TRACE_POINTS) {
            m_tracePath.removeFirst();
        }
    }

    // カメラをドローンに追従（スムーズ）
    m_cameraTarget = m_cameraTarget * 0.95f + m_dronePos * 0.05f;

    update();
}

void MapView3D::clearTrace()
{
    m_tracePath.clear();
    update();
}

// ============================================================
// OpenGL
// ============================================================

void MapView3D::initializeGL()
{
    initializeOpenGLFunctions();
    glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
}

void MapView3D::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

void MapView3D::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // ===== Projection =====
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    float aspect = static_cast<float>(width()) / static_cast<float>(height());
    float fov = 45.0f;
    float nearP = 0.1f, farP = 1000.0f;
    float top = nearP * qTan(qDegreesToRadians(fov / 2.0f));
    float right = top * aspect;
    glFrustum(-right, right, -top, top, nearP, farP);

    // ===== View (Camera) =====
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    // カメラ位置を球面座標で計算
    float radX = qDegreesToRadians(m_cameraAngleX);
    float radY = qDegreesToRadians(m_cameraAngleY);
    float camX = m_cameraTarget.x() + m_cameraDistance * qCos(radX) * qSin(radY);
    float camY = m_cameraTarget.y() + m_cameraDistance * qSin(radX);
    float camZ = m_cameraTarget.z() + m_cameraDistance * qCos(radX) * qCos(radY);

    // gluLookAt相当
    QVector3D eye(camX, camY, camZ);
    QVector3D center = m_cameraTarget;
    QVector3D up(0, 1, 0);

    QVector3D f = (center - eye).normalized();
    QVector3D s = QVector3D::crossProduct(f, up).normalized();
    QVector3D u = QVector3D::crossProduct(s, f);

    float lookAt[16] = {
        s.x(),  u.x(), -f.x(), 0,
        s.y(),  u.y(), -f.y(), 0,
        s.z(),  u.z(), -f.z(), 0,
        -QVector3D::dotProduct(s, eye),
        -QVector3D::dotProduct(u, eye),
         QVector3D::dotProduct(f, eye),
        1
    };
    glMultMatrixf(lookAt);

    // ===== シーン描画 =====
    drawGrid();
    drawAxes();
    drawHomeMarker();
    drawTrace();
    drawShadow();
    drawAltitudeLine();
    drawWaypoints();
    drawDrone();

    // HUD (2D overlay)
    drawHUD();
}

// ============================================================
// 描画関数
// ============================================================

void MapView3D::drawGrid()
{
    float gridSize = 100.0f;
    float step = 5.0f;

    // メイングリッド
    glLineWidth(1.0f);
    glBegin(GL_LINES);
    for (float i = -gridSize; i <= gridSize; i += step) {
        bool major = (fmod(qAbs(i), 25.0f) < 0.01f);

        if (major) {
            glColor4f(0.25f, 0.25f, 0.30f, 0.8f);
        } else {
            glColor4f(0.15f, 0.15f, 0.18f, 0.5f);
        }

        // X方向 (East-West)
        glVertex3f(i, 0, -gridSize);
        glVertex3f(i, 0, gridSize);
        // Z方向 (North-South)
        glVertex3f(-gridSize, 0, i);
        glVertex3f(gridSize, 0, i);
    }
    glEnd();

    // グラウンドプレーン（半透明）
    glBegin(GL_QUADS);
    glColor4f(0.05f, 0.08f, 0.05f, 0.3f);
    glVertex3f(-gridSize, -0.01f, -gridSize);
    glVertex3f( gridSize, -0.01f, -gridSize);
    glVertex3f( gridSize, -0.01f,  gridSize);
    glVertex3f(-gridSize, -0.01f,  gridSize);
    glEnd();
}

void MapView3D::drawAxes()
{
    glLineWidth(2.0f);
    float len = 10.0f;

    glBegin(GL_LINES);
    // X軸 (East) - 赤
    glColor3f(0.8f, 0.2f, 0.2f);
    glVertex3f(0, 0.01f, 0); glVertex3f(len, 0.01f, 0);
    // Y軸 (Up) - 緑
    glColor3f(0.2f, 0.8f, 0.2f);
    glVertex3f(0, 0.01f, 0); glVertex3f(0, len, 0);
    // Z軸 (South) - 青
    glColor3f(0.2f, 0.2f, 0.8f);
    glVertex3f(0, 0.01f, 0); glVertex3f(0, 0.01f, -len);
    glEnd();
}

void MapView3D::drawHomeMarker()
{
    // ホーム位置にマーカー（緑の十字+円）
    glLineWidth(2.0f);
    glColor4f(0.18f, 0.8f, 0.44f, 0.9f);

    float r = 1.5f;
    // 円
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < 36; i++) {
        float angle = qDegreesToRadians(i * 10.0f);
        glVertex3f(r * qCos(angle), 0.02f, r * qSin(angle));
    }
    glEnd();

    // H マーク
    glBegin(GL_LINES);
    glVertex3f(-0.5f, 0.02f, -0.7f); glVertex3f(-0.5f, 0.02f, 0.7f);
    glVertex3f( 0.5f, 0.02f, -0.7f); glVertex3f( 0.5f, 0.02f, 0.7f);
    glVertex3f(-0.5f, 0.02f, 0.0f);  glVertex3f( 0.5f, 0.02f, 0.0f);
    glEnd();
}

void MapView3D::drawDrone()
{
    glPushMatrix();
    glTranslatef(m_dronePos.x(), m_dronePos.y(), m_dronePos.z());
    glRotatef(-m_droneYaw, 0, 1, 0);    // ヨー
    glRotatef(m_dronePitch, 1, 0, 0);    // ピッチ
    glRotatef(-m_droneRoll, 0, 0, 1);    // ロール

    float bodySize = 0.8f;
    float armLen = 1.5f;

    // === メインボディ（中央の箱） ===
    glColor4f(0.3f, 0.3f, 0.35f, 1.0f);
    float bs = bodySize * 0.5f;
    float bh = 0.15f;
    glBegin(GL_QUADS);
    // 上面
    glColor4f(0.35f, 0.35f, 0.40f, 1.0f);
    glVertex3f(-bs, bh, -bs); glVertex3f(bs, bh, -bs);
    glVertex3f(bs, bh, bs);  glVertex3f(-bs, bh, bs);
    // 下面
    glColor4f(0.25f, 0.25f, 0.30f, 1.0f);
    glVertex3f(-bs, -bh, -bs); glVertex3f(bs, -bh, -bs);
    glVertex3f(bs, -bh, bs);  glVertex3f(-bs, -bh, bs);
    // 前面
    glColor4f(0.9f, 0.3f, 0.2f, 1.0f); // 前方は赤
    glVertex3f(-bs, -bh, -bs); glVertex3f(bs, -bh, -bs);
    glVertex3f(bs, bh, -bs);  glVertex3f(-bs, bh, -bs);
    // 背面
    glColor4f(0.3f, 0.3f, 0.35f, 1.0f);
    glVertex3f(-bs, -bh, bs); glVertex3f(bs, -bh, bs);
    glVertex3f(bs, bh, bs);  glVertex3f(-bs, bh, bs);
    // 左面
    glVertex3f(-bs, -bh, -bs); glVertex3f(-bs, -bh, bs);
    glVertex3f(-bs, bh, bs);  glVertex3f(-bs, bh, -bs);
    // 右面
    glVertex3f(bs, -bh, -bs); glVertex3f(bs, -bh, bs);
    glVertex3f(bs, bh, bs);  glVertex3f(bs, bh, -bs);
    glEnd();

    // === アーム (4本 X字型) ===
    float armW = 0.08f;
    float armH = 0.05f;
    struct ArmDir { float dx; float dz; };
    ArmDir arms[4] = {{1,1}, {1,-1}, {-1,1}, {-1,-1}};

    for (auto &arm : arms) {
        float ex = arm.dx * armLen * 0.707f;
        float ez = arm.dz * armLen * 0.707f;

        glColor4f(0.4f, 0.4f, 0.45f, 1.0f);
        glBegin(GL_QUADS);
        // 上面
        glVertex3f(-armW * arm.dz + 0, armH, -armW * (-arm.dx) + 0);
        glVertex3f( armW * arm.dz + 0, armH,  armW * (-arm.dx) + 0);
        glVertex3f( armW * arm.dz + ex, armH,  armW * (-arm.dx) + ez);
        glVertex3f(-armW * arm.dz + ex, armH, -armW * (-arm.dx) + ez);
        // 下面
        glVertex3f(-armW * arm.dz + 0, -armH, -armW * (-arm.dx) + 0);
        glVertex3f( armW * arm.dz + 0, -armH,  armW * (-arm.dx) + 0);
        glVertex3f( armW * arm.dz + ex, -armH,  armW * (-arm.dx) + ez);
        glVertex3f(-armW * arm.dz + ex, -armH, -armW * (-arm.dx) + ez);
        glEnd();

        // === モーター（シリンダ風の円柱） ===
        float mR = 0.15f;
        float mH2 = 0.2f;
        glColor4f(0.5f, 0.5f, 0.55f, 1.0f);
        glBegin(GL_QUAD_STRIP);
        for (int i = 0; i <= 12; i++) {
            float a = qDegreesToRadians(i * 30.0f);
            float mx = ex + mR * qCos(a);
            float mz = ez + mR * qSin(a);
            glVertex3f(mx, mH2, mz);
            glVertex3f(mx, armH, mz);
        }
        glEnd();

        // === プロペラ ===
        float propR = 0.9f;
        glPushMatrix();
        glTranslatef(ex, mH2 + 0.02f, ez);
        glRotatef(m_propellerAngle * arm.dx, 0, 1, 0);

        // 前方アーム: 赤プロペラ、後方: 青プロペラ
        if (arm.dz < 0) {
            glColor4f(0.9f, 0.3f, 0.2f, 0.6f);
        } else {
            glColor4f(0.2f, 0.5f, 0.9f, 0.6f);
        }

        glBegin(GL_TRIANGLES);
        // ブレード1
        glVertex3f(0, 0, 0);
        glVertex3f(propR, 0, 0.06f);
        glVertex3f(propR, 0, -0.06f);
        // ブレード2
        glVertex3f(0, 0, 0);
        glVertex3f(-propR, 0, 0.06f);
        glVertex3f(-propR, 0, -0.06f);
        glEnd();

        glPopMatrix();
    }

    // === LED (前方 緑、後方 赤) ===
    glPointSize(6.0f);
    glBegin(GL_POINTS);
    glColor3f(0.0f, 1.0f, 0.0f);
    glVertex3f(0, -bh, -bs - 0.1f);
    glColor3f(1.0f, 0.0f, 0.0f);
    glVertex3f(0, -bh, bs + 0.1f);
    glEnd();

    glPopMatrix();
}

void MapView3D::drawTrace()
{
    if (m_tracePath.size() < 2) return;

    glLineWidth(2.0f);
    glBegin(GL_LINE_STRIP);
    for (int i = 0; i < m_tracePath.size(); i++) {
        float t = static_cast<float>(i) / m_tracePath.size();
        // 古い部分は暗く、新しい部分は明るく
        glColor4f(0.2f + 0.6f * t, 0.4f + 0.4f * t, 0.9f, 0.3f + 0.7f * t);
        glVertex3f(m_tracePath[i].x(), m_tracePath[i].y(), m_tracePath[i].z());
    }
    glEnd();
}

void MapView3D::drawShadow()
{
    // ドローンの地面への投影（影）
    float shadowSize = 1.0f;
    glBegin(GL_QUADS);
    glColor4f(0.0f, 0.0f, 0.0f, 0.3f);
    glVertex3f(m_dronePos.x() - shadowSize, 0.01f, m_dronePos.z() - shadowSize);
    glVertex3f(m_dronePos.x() + shadowSize, 0.01f, m_dronePos.z() - shadowSize);
    glVertex3f(m_dronePos.x() + shadowSize, 0.01f, m_dronePos.z() + shadowSize);
    glVertex3f(m_dronePos.x() - shadowSize, 0.01f, m_dronePos.z() + shadowSize);
    glEnd();
}

void MapView3D::drawAltitudeLine()
{
    if (m_dronePos.y() < 0.1f) return;

    // ドローン位置から地面への点線
    glLineWidth(1.0f);
    glEnable(GL_LINE_STIPPLE);
    glLineStipple(2, 0xAAAA);
    glColor4f(1.0f, 0.78f, 0.0f, 0.5f);
    glBegin(GL_LINES);
    glVertex3f(m_dronePos.x(), m_dronePos.y(), m_dronePos.z());
    glVertex3f(m_dronePos.x(), 0.0f, m_dronePos.z());
    glEnd();
    glDisable(GL_LINE_STIPPLE);
}

void MapView3D::drawHUD()
{
    // QPainterで2Dオーバーレイを描画
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // 方位コンパス表示（右上）
    int compassX = width() - 70;
    int compassY = 70;
    int compassR = 40;

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 120));
    painter.drawEllipse(QPoint(compassX, compassY), compassR + 5, compassR + 5);

    painter.setPen(QPen(QColor(200, 200, 200), 1));
    painter.drawEllipse(QPoint(compassX, compassY), compassR, compassR);

    QFont f = painter.font();
    f.setPixelSize(12);
    f.setBold(true);
    painter.setFont(f);

    // N/S/E/W
    struct { const char* text; float angle; QColor color; } labels[] = {
        {"N", 0, QColor(230, 80, 80)},
        {"E", 90, QColor(200, 200, 200)},
        {"S", 180, QColor(200, 200, 200)},
        {"W", 270, QColor(200, 200, 200)}
    };
    for (auto &l : labels) {
        float a = qDegreesToRadians(l.angle - m_droneYaw - 90);
        int lx = compassX + static_cast<int>((compassR - 12) * qCos(a));
        int ly = compassY + static_cast<int>((compassR - 12) * qSin(a));
        painter.setPen(l.color);
        painter.drawText(lx - 6, ly - 6, 12, 12, Qt::AlignCenter, l.text);
    }

    // ドローン方向マーカー
    painter.setPen(QPen(QColor(255, 200, 0), 2));
    painter.drawLine(compassX, compassY - compassR + 8, compassX - 5, compassY - compassR + 16);
    painter.drawLine(compassX, compassY - compassR + 8, compassX + 5, compassY - compassR + 16);

    // 高度表示（左上）
    f.setPixelSize(14);
    painter.setFont(f);

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 120));
    painter.drawRoundedRect(10, 10, 150, 60, 6, 6);

    painter.setPen(QColor(150, 150, 150));
    f.setPixelSize(10);
    painter.setFont(f);
    painter.drawText(18, 28, "ALT");
    painter.drawText(18, 52, "SPD");

    f.setPixelSize(16);
    f.setBold(true);
    painter.setFont(f);
    painter.setPen(QColor(0, 255, 100));
    painter.drawText(50, 30, QString::number(static_cast<double>(m_dronePos.y()), 'f', 1) + " m");

    float spd = 0;
    if (m_tracePath.size() >= 2) {
        QVector3D diff = m_tracePath.last() - m_tracePath[m_tracePath.size() - 2];
        spd = diff.length() * 30.0f; // 30fps * distance
    }
    painter.drawText(50, 54, QString::number(static_cast<double>(spd), 'f', 1) + " m/s");

    // 操作ヘルプ（左下）
    f.setPixelSize(10);
    f.setBold(false);
    painter.setFont(f);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 100));
    painter.drawRoundedRect(10, height() - 55, 200, 45, 4, 4);
    painter.setPen(QColor(120, 120, 130));
    painter.drawText(18, height() - 38, "左ドラッグ: 回転 | 右ドラッグ: パン");
    painter.drawText(18, height() - 22, "ホイール: ズーム");

    painter.end();
}

// ============================================================
// マウスイベント
// ============================================================

void MapView3D::mousePressEvent(QMouseEvent *event)
{
    m_lastMousePos = event->pos();
    if (event->button() == Qt::LeftButton) {
        m_rotating = true;
    } else if (event->button() == Qt::RightButton || event->button() == Qt::MiddleButton) {
        m_panning = true;
    }
}

void MapView3D::mouseMoveEvent(QMouseEvent *event)
{
    int dx = event->pos().x() - m_lastMousePos.x();
    int dy = event->pos().y() - m_lastMousePos.y();
    m_lastMousePos = event->pos();

    if (m_rotating) {
        m_cameraAngleY += dx * 0.5f;
        m_cameraAngleX += dy * 0.3f;
        m_cameraAngleX = qBound(-89.0f, m_cameraAngleX, 89.0f);
        update();
    }
    if (m_panning) {
        float radY = qDegreesToRadians(m_cameraAngleY);
        float speed = m_cameraDistance * 0.005f;
        m_cameraTarget.setX(m_cameraTarget.x() - dx * speed * qCos(radY));
        m_cameraTarget.setZ(m_cameraTarget.z() + dx * speed * qSin(radY));
        m_cameraTarget.setY(m_cameraTarget.y() + dy * speed);
        update();
    }
}

void MapView3D::mouseReleaseEvent(QMouseEvent *)
{
    m_rotating = false;
    m_panning = false;
}

void MapView3D::wheelEvent(QWheelEvent *event)
{
    float delta = event->angleDelta().y() / 120.0f;
    m_cameraDistance *= (1.0f - delta * 0.1f);
    m_cameraDistance = qBound(5.0f, m_cameraDistance, 200.0f);
    update();
}

// ============================================================
// ウェイポイント
// ============================================================

void MapView3D::setWaypoints(const QVector<MissionItem> &items)
{
    m_waypoints.clear();
    for (int i = 0; i < items.size(); i++) {
        WpData wp;
        wp.pos = geoToLocal(items[i].latitude, items[i].longitude, items[i].altitude);
        wp.command = items[i].command;
        wp.seq = i;
        m_waypoints.append(wp);
    }
    update();
}

void MapView3D::setActiveWaypoint(int index)
{
    m_activeWpIndex = index;
    update();
}

void MapView3D::drawWaypoints()
{
    if (m_waypoints.isEmpty()) return;

    // 経路ライン（破線風に）
    glLineWidth(2.0f);
    glBegin(GL_LINE_STRIP);
    for (int i = 0; i < m_waypoints.size(); i++) {
        const auto &wp = m_waypoints[i];
        if (i == m_activeWpIndex) {
            glColor4f(1.0f, 0.78f, 0.0f, 0.9f); // 黄
        } else {
            glColor4f(0.9f, 0.3f, 0.24f, 0.7f); // 赤
        }
        glVertex3f(wp.pos.x(), wp.pos.y(), wp.pos.z());
    }
    glEnd();

    // WPマーカー（球体の代わりに八角形ダイヤモンド）
    for (int i = 0; i < m_waypoints.size(); i++) {
        const auto &wp = m_waypoints[i];

        bool isActive = (i == m_activeWpIndex);
        float r = isActive ? 1.2f : 0.8f;

        glPushMatrix();
        glTranslatef(wp.pos.x(), wp.pos.y(), wp.pos.z());

        // ポイントマーカー
        if (isActive) {
            glColor4f(1.0f, 0.78f, 0.0f, 0.9f); // 黄
        } else {
            glColor4f(0.9f, 0.3f, 0.24f, 0.8f); // 赤
        }

        // 3Dダイヤモンド（上下のピラミッド）
        float h = r * 0.7f;
        glBegin(GL_TRIANGLE_FAN);
        glVertex3f(0, h, 0); // 頂点
        for (int j = 0; j <= 8; j++) {
            float a = qDegreesToRadians(j * 45.0f);
            glVertex3f(r * qCos(a), 0, r * qSin(a));
        }
        glEnd();

        glBegin(GL_TRIANGLE_FAN);
        glVertex3f(0, -h, 0); // 底点
        for (int j = 0; j <= 8; j++) {
            float a = qDegreesToRadians(j * 45.0f);
            glVertex3f(r * qCos(a), 0, r * qSin(a));
        }
        glEnd();

        // 地面への垂直線
        glLineWidth(1.0f);
        glEnable(GL_LINE_STIPPLE);
        glLineStipple(2, 0xAAAA);
        glColor4f(0.9f, 0.3f, 0.24f, 0.3f);
        glBegin(GL_LINES);
        glVertex3f(0, 0, 0);
        glVertex3f(0, -wp.pos.y(), 0);
        glEnd();
        glDisable(GL_LINE_STIPPLE);

        // 地面マーカー
        glColor4f(0.9f, 0.3f, 0.24f, 0.2f);
        glBegin(GL_LINE_LOOP);
        for (int j = 0; j < 12; j++) {
            float a = qDegreesToRadians(j * 30.0f);
            glVertex3f(r * 0.6f * qCos(a), -wp.pos.y() + 0.02f, r * 0.6f * qSin(a));
        }
        glEnd();

        glPopMatrix();
    }
}
