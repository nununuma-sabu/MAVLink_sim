#include "MapView3D.h"
#include "core/MissionManager.h"
#include "core/GeoUtils.h"
#include <QtMath>
#include <QPainter>
#include <QVector4D>
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

    m_buildingProvider = new BuildingProvider(this);
    connect(m_buildingProvider, &BuildingProvider::buildingsReady,
            this, [this](const QVector<BuildingData> &buildings, const QString &source) {
        m_buildings = buildings;
        m_buildingStatus = QString("建物データ: %1 (%2件)").arg(source).arg(buildings.size());
        qDebug() << "[MapView3D] 建物データ適用:" << buildings.size() << "件 source:" << source;
        update();
    });
    connect(m_buildingProvider, &BuildingProvider::pathsReady,
            this, [this](const QVector<GroundPathData> &paths, const QString &source) {
        m_groundPaths = paths;
        qDebug() << "[MapView3D] 地表パス適用:" << paths.size() << "件 source:" << source;
        update();
    });
    connect(m_buildingProvider, &BuildingProvider::statusMessage,
            this, [this](const QString &message) {
        m_buildingStatus = message;
        qDebug() << "[MapView3D]" << message;
        update();
    });
    m_buildingProvider->loadForOrigin(m_homeLat, m_homeLon, 300);
}

QVector3D MapView3D::geoToLocal(double lat, double lon, double alt) const
{
    const auto offset = Geo::geoToRelative(m_homeLat, m_homeLon, lat, lon);
    // OpenGL座標: X=East, Y=Up, Z=South (右手系)
    return QVector3D(static_cast<float>(offset.east),
                     static_cast<float>(alt),
                     static_cast<float>(-offset.north));
}

QVector3D MapView3D::buildingPointToLocal(const QVector2D &point, float altitude) const
{
    // 建物JSONはホーム基準の East/North メートルで保持する。
    return QVector3D(point.x(), altitude, -point.y());
}

QVector3D MapView3D::pathPointToLocal(const QVector2D &point, float altitude) const
{
    return QVector3D(point.x(), altitude, -point.y());
}

QVector3D MapView3D::buildingCenter(const BuildingData &building) const
{
    QVector3D center(0, 0, 0);
    if (building.footprint.isEmpty()) {
        return center;
    }

    for (const QVector2D &point : building.footprint) {
        center += buildingPointToLocal(point, building.height);
    }
    return center / static_cast<float>(building.footprint.size());
}

bool MapView3D::worldToScreen(const QVector3D &world, QPointF &screen) const
{
    const float aspect = height() > 0
        ? static_cast<float>(width()) / static_cast<float>(height())
        : 1.0f;
    const float fov = 45.0f;
    const float nearP = 0.1f;
    const float farP = 1000.0f;
    const float top = nearP * qTan(qDegreesToRadians(fov / 2.0f));
    const float right = top * aspect;

    QMatrix4x4 projection;
    projection.frustum(-right, right, -top, top, nearP, farP);

    const float radX = qDegreesToRadians(m_cameraAngleX);
    const float radY = qDegreesToRadians(m_cameraAngleY);
    const QVector3D eye(
        m_cameraTarget.x() + m_cameraDistance * qCos(radX) * qSin(radY),
        m_cameraTarget.y() + m_cameraDistance * qSin(radX),
        m_cameraTarget.z() + m_cameraDistance * qCos(radX) * qCos(radY));

    QMatrix4x4 view;
    view.lookAt(eye, m_cameraTarget, QVector3D(0, 1, 0));

    const QVector4D clip = projection * view * QVector4D(world, 1.0f);
    if (clip.w() <= 0.0f) {
        return false;
    }

    const QVector3D ndc = clip.toVector3DAffine();
    if (ndc.z() < -1.0f || ndc.z() > 1.0f) {
        return false;
    }

    screen = QPointF((ndc.x() * 0.5f + 0.5f) * width(),
                     (0.5f - ndc.y() * 0.5f) * height());
    return screen.x() >= -80.0 && screen.x() <= width() + 80.0 &&
           screen.y() >= -30.0 && screen.y() <= height() + 30.0;
}

float MapView3D::lightForWall(const QVector3D &a, const QVector3D &b, int wallIndex) const
{
    QVector3D edge = b - a;
    edge.setY(0.0f);
    if (edge.lengthSquared() < 0.0001f) {
        return 0.7f;
    }
    edge.normalize();
    QVector3D normal(-edge.z(), 0.0f, edge.x());
    const QVector3D sunDir = QVector3D(-0.45f, 0.0f, -0.75f).normalized();
    const float direct = qMax(0.0f, QVector3D::dotProduct(normal, sunDir));
    const float variation = (wallIndex % 2 == 0) ? 0.04f : -0.03f;
    return qBound(0.45f, 0.58f + direct * 0.42f + variation, 1.08f);
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
    glClearColor(0.42f, 0.52f, 0.62f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
    glEnable(GL_FOG);
    GLfloat fogColor[] = {0.42f, 0.52f, 0.62f, 1.0f};
    glFogfv(GL_FOG_COLOR, fogColor);
    glFogi(GL_FOG_MODE, GL_LINEAR);
    glFogf(GL_FOG_START, 260.0f);
    glFogf(GL_FOG_END, 760.0f);
}

void MapView3D::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

void MapView3D::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    drawSkyGradient();

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
    glEnable(GL_FOG);
    drawGrid();
    drawGroundPaths();
    drawBuildings();
    drawAxes();
    drawHomeMarker();
    drawTrace();
    drawShadow();
    drawAltitudeLine();
    drawWaypoints();
    drawDrone();
    glDisable(GL_FOG);

    // HUD (2D overlay)
    drawHUD();
}

// ============================================================
// 描画関数
// ============================================================

void MapView3D::drawSkyGradient()
{
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, 1, 0, 1, -1, 1);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_FOG);
    glBegin(GL_QUADS);
    glColor3f(0.33f, 0.43f, 0.54f);
    glVertex2f(0.0f, 0.0f);
    glVertex2f(1.0f, 0.0f);
    glColor3f(0.62f, 0.70f, 0.78f);
    glVertex2f(1.0f, 1.0f);
    glVertex2f(0.0f, 1.0f);
    glEnd();
    glEnable(GL_DEPTH_TEST);

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
}

void MapView3D::drawGrid()
{
    float gridSize = 420.0f;
    float step = 10.0f;

    drawGroundTexture();

    // メイングリッド
    glLineWidth(1.0f);
    glBegin(GL_LINES);
    for (float i = -gridSize; i <= gridSize; i += step) {
        bool major = (fmod(qAbs(i), 50.0f) < 0.01f);

        if (major) {
            glColor4f(0.24f, 0.30f, 0.28f, 0.34f);
        } else {
            glColor4f(0.17f, 0.22f, 0.20f, 0.16f);
        }

        // X方向 (East-West)
        glVertex3f(i, 0, -gridSize);
        glVertex3f(i, 0, gridSize);
        // Z方向 (North-South)
        glVertex3f(-gridSize, 0, i);
        glVertex3f(gridSize, 0, i);
    }
    glEnd();

}

void MapView3D::drawGroundTexture()
{
    const float gridSize = 420.0f;

    // グラウンドプレーン
    glBegin(GL_QUADS);
    glColor4f(0.045f, 0.078f, 0.058f, 0.96f);
    glVertex3f(-gridSize, -0.01f, -gridSize);
    glVertex3f( gridSize, -0.01f, -gridSize);
    glVertex3f( gridSize, -0.01f,  gridSize);
    glVertex3f(-gridSize, -0.01f,  gridSize);
    glEnd();

    // 舗装・緑地・敷地のわずかな色むら。固定パターンなので毎フレーム安定する。
    const float cell = 28.0f;
    glBegin(GL_QUADS);
    for (float x = -gridSize; x < gridSize; x += cell) {
        for (float z = -gridSize; z < gridSize; z += cell) {
            const int ix = static_cast<int>((x + gridSize) / cell);
            const int iz = static_cast<int>((z + gridSize) / cell);
            const int pattern = qAbs((ix * 37 + iz * 17 + ix * iz * 3) % 11);

            QColor tone;
            float alpha = 0.22f;
            if (pattern == 0 || pattern == 7) {
                tone = QColor("#263226"); // 濃い緑地
                alpha = 0.30f;
            } else if (pattern == 1 || pattern == 8) {
                tone = QColor("#394036"); // 敷地のくすみ
                alpha = 0.22f;
            } else if (pattern == 2) {
                tone = QColor("#4a4941"); // 舗装面
                alpha = 0.18f;
            } else {
                tone = QColor("#1f2c23");
                alpha = 0.16f;
            }

            const float inset = 0.9f + static_cast<float>(pattern % 3) * 0.35f;
            glColor4f(tone.redF(), tone.greenF(), tone.blueF(), alpha);
            glVertex3f(x + inset, 0.0f, z + inset);
            glVertex3f(x + cell - inset, 0.0f, z + inset);
            glVertex3f(x + cell - inset, 0.0f, z + cell - inset);
            glVertex3f(x + inset, 0.0f, z + cell - inset);
        }
    }
    glEnd();

    // 区画境界のような薄いライン
    glLineWidth(0.8f);
    glBegin(GL_LINES);
    glColor4f(0.55f, 0.58f, 0.49f, 0.10f);
    for (float i = -gridSize; i <= gridSize; i += 28.0f) {
        glVertex3f(i, 0.006f, -gridSize);
        glVertex3f(i, 0.006f, gridSize);
        glVertex3f(-gridSize, 0.006f, i);
        glVertex3f(gridSize, 0.006f, i);
    }
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

void MapView3D::drawGroundPaths()
{
    if (m_groundPaths.isEmpty()) return;

    for (const GroundPathData &path : m_groundPaths) {
        if (path.points.size() < 2) continue;

        if (path.type == "railway") {
            drawRailwayDetails(path);
        } else {
            drawPathBand(path);
        }
    }
}

void MapView3D::drawPathBand(const GroundPathData &path)
{
    const float halfWidth = qBound(1.0f, path.width, 14.0f) * 0.5f;
    const QColor color = path.color;

    for (int i = 0; i < path.points.size() - 1; i++) {
        const QVector3D a = pathPointToLocal(path.points[i], 0.025f);
        const QVector3D b = pathPointToLocal(path.points[i + 1], 0.025f);
        QVector3D dir = b - a;
        dir.setY(0.0f);
        const float len = dir.length();
        if (len < 0.01f) continue;
        dir /= len;

        const QVector3D normal(-dir.z(), 0.0f, dir.x());
        const QVector3D aL = a + normal * halfWidth;
        const QVector3D aR = a - normal * halfWidth;
        const QVector3D bR = b - normal * halfWidth;
        const QVector3D bL = b + normal * halfWidth;

        glBegin(GL_QUADS);
        glColor4f(color.redF(), color.greenF(), color.blueF(), 0.92f);
        glVertex3f(aL.x(), aL.y(), aL.z());
        glVertex3f(aR.x(), aR.y(), aR.z());
        glVertex3f(bR.x(), bR.y(), bR.z());
        glVertex3f(bL.x(), bL.y(), bL.z());
        glEnd();
    }

    drawRoadMarkings(path, halfWidth);
}

void MapView3D::drawRoadMarkings(const GroundPathData &path, float halfWidth)
{
    if (path.width < 3.5f || path.type == "footway" || path.type == "path") {
        return;
    }

    for (int i = 0; i < path.points.size() - 1; i++) {
        const QVector3D a = pathPointToLocal(path.points[i], 0.065f);
        const QVector3D b = pathPointToLocal(path.points[i + 1], 0.065f);
        QVector3D dir = b - a;
        dir.setY(0.0f);
        const float len = dir.length();
        if (len < 0.01f) continue;
        dir /= len;

        const QVector3D normal(-dir.z(), 0.0f, dir.x());
        const float edgeOffset = qMax(halfWidth - 0.42f, 0.6f);

        glLineWidth(1.2f);
        glBegin(GL_LINES);
        glColor4f(0.88f, 0.86f, 0.74f, 0.34f);
        const QVector3D aL = a + normal * edgeOffset;
        const QVector3D bL = b + normal * edgeOffset;
        const QVector3D aR = a - normal * edgeOffset;
        const QVector3D bR = b - normal * edgeOffset;
        glVertex3f(aL.x(), aL.y(), aL.z());
        glVertex3f(bL.x(), bL.y(), bL.z());
        glVertex3f(aR.x(), aR.y(), aR.z());
        glVertex3f(bR.x(), bR.y(), bR.z());
        glEnd();

        if (path.width >= 5.0f) {
            const float dashLength = qBound(2.6f, len * 0.08f, 5.0f);
            const float dashGap = dashLength * 1.25f;
            const float halfDashWidth = 0.12f;

            glBegin(GL_QUADS);
            glColor4f(0.94f, 0.90f, 0.66f, 0.58f);
            for (float t = dashGap * 0.5f; t < len - dashLength * 0.5f; t += dashLength + dashGap) {
                const QVector3D c0 = a + dir * t;
                const QVector3D c1 = a + dir * qMin(t + dashLength, len);
                const QVector3D l0 = c0 + normal * halfDashWidth;
                const QVector3D r0 = c0 - normal * halfDashWidth;
                const QVector3D r1 = c1 - normal * halfDashWidth;
                const QVector3D l1 = c1 + normal * halfDashWidth;
                glVertex3f(l0.x(), l0.y(), l0.z());
                glVertex3f(r0.x(), r0.y(), r0.z());
                glVertex3f(r1.x(), r1.y(), r1.z());
                glVertex3f(l1.x(), l1.y(), l1.z());
            }
            glEnd();
        }

        if (path.width >= 4.0f && (i == 0 || i == path.points.size() - 2 || len > 48.0f)) {
            const QVector3D crossCenter = a + dir * qBound(5.0f, len * 0.20f, 12.0f);
            drawCrosswalk(crossCenter, dir, normal, halfWidth);
        }
    }
}

void MapView3D::drawCrosswalk(const QVector3D &center, const QVector3D &dir,
                              const QVector3D &normal, float halfWidth)
{
    const int stripeCount = qBound(4, static_cast<int>(halfWidth * 0.9f), 8);
    const float stripeLength = qMin(halfWidth * 1.72f, 9.0f);
    const float stripeWidth = 0.46f;
    const float gap = 0.52f;
    const float totalWidth = stripeCount * stripeWidth + (stripeCount - 1) * gap;
    const QVector3D start = center - dir * (totalWidth * 0.5f);

    glBegin(GL_QUADS);
    glColor4f(0.90f, 0.90f, 0.82f, 0.54f);
    for (int i = 0; i < stripeCount; i++) {
        const QVector3D c = start + dir * (i * (stripeWidth + gap) + stripeWidth * 0.5f);
        const QVector3D p0 = c - normal * (stripeLength * 0.5f) - dir * (stripeWidth * 0.5f);
        const QVector3D p1 = c + normal * (stripeLength * 0.5f) - dir * (stripeWidth * 0.5f);
        const QVector3D p2 = c + normal * (stripeLength * 0.5f) + dir * (stripeWidth * 0.5f);
        const QVector3D p3 = c - normal * (stripeLength * 0.5f) + dir * (stripeWidth * 0.5f);
        glVertex3f(p0.x(), p0.y() + 0.012f, p0.z());
        glVertex3f(p1.x(), p1.y() + 0.012f, p1.z());
        glVertex3f(p2.x(), p2.y() + 0.012f, p2.z());
        glVertex3f(p3.x(), p3.y() + 0.012f, p3.z());
    }
    glEnd();
}

void MapView3D::drawRailwayDetails(const GroundPathData &path)
{
    const float railOffset = qMax(1.0f, path.width * 0.22f);
    const QColor bed = path.color;

    for (int i = 0; i < path.points.size() - 1; i++) {
        const QVector3D a = pathPointToLocal(path.points[i], 0.03f);
        const QVector3D b = pathPointToLocal(path.points[i + 1], 0.03f);
        QVector3D dir = b - a;
        dir.setY(0.0f);
        const float len = dir.length();
        if (len < 0.01f) continue;
        dir /= len;

        const QVector3D normal(-dir.z(), 0.0f, dir.x());
        const float bedHalfWidth = qBound(2.5f, path.width * 0.65f, 5.0f);

        glBegin(GL_QUADS);
        glColor4f(bed.redF() * 0.75f, bed.greenF() * 0.75f, bed.blueF() * 0.75f, 0.82f);
        glVertex3f((a + normal * bedHalfWidth).x(), a.y(), (a + normal * bedHalfWidth).z());
        glVertex3f((a - normal * bedHalfWidth).x(), a.y(), (a - normal * bedHalfWidth).z());
        glVertex3f((b - normal * bedHalfWidth).x(), b.y(), (b - normal * bedHalfWidth).z());
        glVertex3f((b + normal * bedHalfWidth).x(), b.y(), (b + normal * bedHalfWidth).z());
        glEnd();

        glLineWidth(2.0f);
        glBegin(GL_LINES);
        glColor4f(0.76f, 0.72f, 0.66f, 0.95f);
        const QVector3D aRailL = a + normal * railOffset;
        const QVector3D bRailL = b + normal * railOffset;
        const QVector3D aRailR = a - normal * railOffset;
        const QVector3D bRailR = b - normal * railOffset;
        glVertex3f(aRailL.x(), aRailL.y() + 0.04f, aRailL.z());
        glVertex3f(bRailL.x(), bRailL.y() + 0.04f, bRailL.z());
        glVertex3f(aRailR.x(), aRailR.y() + 0.04f, aRailR.z());
        glVertex3f(bRailR.x(), bRailR.y() + 0.04f, bRailR.z());
        glEnd();

        const int sleeperCount = qBound(1, static_cast<int>(len / 5.0f), 24);
        glLineWidth(1.5f);
        glBegin(GL_LINES);
        glColor4f(0.42f, 0.34f, 0.28f, 0.85f);
        for (int s = 0; s <= sleeperCount; s++) {
            const QVector3D c = a + dir * (len * s / sleeperCount);
            const QVector3D left = c + normal * (railOffset + 0.45f);
            const QVector3D right = c - normal * (railOffset + 0.45f);
            glVertex3f(left.x(), c.y() + 0.055f, left.z());
            glVertex3f(right.x(), c.y() + 0.055f, right.z());
        }
        glEnd();
    }
}

void MapView3D::drawBuildings()
{
    if (m_buildings.isEmpty()) return;

    for (const auto &building : m_buildings) {
        if (building.footprint.size() < 3) continue;

        const QColor base = building.color;
        const float h = building.height;
        const float r = base.redF();
        const float g = base.greenF();
        const float b = base.blueF();

        // 壁面
        glBegin(GL_QUADS);
        for (int i = 0; i < building.footprint.size(); i++) {
            const QVector2D &a = building.footprint[i];
            const QVector2D &bpt = building.footprint[(i + 1) % building.footprint.size()];
            const QVector3D a0 = buildingPointToLocal(a, 0.0f);
            const QVector3D b0 = buildingPointToLocal(bpt, 0.0f);
            const QVector3D b1 = buildingPointToLocal(bpt, h);
            const QVector3D a1 = buildingPointToLocal(a, h);

            const float shade = lightForWall(a0, b0, i);
            glColor4f(qMin(r * shade, 1.0f),
                      qMin(g * shade, 1.0f),
                      qMin(b * shade, 1.0f),
                      0.94f);
            glVertex3f(a0.x(), a0.y(), a0.z());
            glVertex3f(b0.x(), b0.y(), b0.z());
            glVertex3f(b1.x(), b1.y(), b1.z());
            glVertex3f(a1.x(), a1.y(), a1.z());
        }
        glEnd();

        drawRoof(building);

        drawBuildingDetails(building);
    }
}

void MapView3D::drawRoof(const BuildingData &building)
{
    if (building.footprint.size() < 3) return;

    const float h = building.height;
    const QColor roof = building.roofColor.isValid()
        ? building.roofColor
        : building.color.lighter(115);
    const float r = roof.redF();
    const float g = roof.greenF();
    const float b = roof.blueF();
    const QString shape = building.roofShape.toLower();

    QVector<QVector3D> topPoints;
    topPoints.reserve(building.footprint.size());
    QVector3D center(0, 0, 0);
    float minX = 0.0f;
    float maxX = 0.0f;
    float minZ = 0.0f;
    float maxZ = 0.0f;

    for (const QVector2D &point : building.footprint) {
        const QVector3D p = buildingPointToLocal(point, h);
        if (topPoints.isEmpty()) {
            minX = maxX = p.x();
            minZ = maxZ = p.z();
        } else {
            minX = qMin(minX, p.x());
            maxX = qMax(maxX, p.x());
            minZ = qMin(minZ, p.z());
            maxZ = qMax(maxZ, p.z());
        }
        topPoints.append(p);
        center += p;
    }
    center /= static_cast<float>(topPoints.size());

    auto drawFlatRoof = [&]() {
        glBegin(GL_TRIANGLE_FAN);
        glColor4f(qMin(r * 1.10f + 0.03f, 1.0f),
                  qMin(g * 1.10f + 0.03f, 1.0f),
                  qMin(b * 1.10f + 0.03f, 1.0f),
                  0.96f);
        glVertex3f(center.x(), h, center.z());
        for (int i = 0; i <= topPoints.size(); i++) {
            const QVector3D p = topPoints[i % topPoints.size()];
            glVertex3f(p.x(), p.y(), p.z());
        }
        glEnd();
    };

    const float spanX = maxX - minX;
    const float spanZ = maxZ - minZ;
    const bool boxRoof = topPoints.size() == 4 && spanX > 3.0f && spanZ > 3.0f;
    const float roofHeight = qBound(1.2f, h * 0.16f, 6.0f);

    if (!boxRoof || shape == "flat") {
        drawFlatRoof();
    } else if (shape == "pyramidal") {
        const QVector3D apex(center.x(), h + roofHeight, center.z());
        glBegin(GL_TRIANGLES);
        for (int i = 0; i < topPoints.size(); i++) {
            const QVector3D a = topPoints[i];
            const QVector3D c = topPoints[(i + 1) % topPoints.size()];
            const float shade = 0.92f + 0.10f * (i % 2);
            glColor4f(qMin(r * shade, 1.0f), qMin(g * shade, 1.0f), qMin(b * shade, 1.0f), 0.98f);
            glVertex3f(a.x(), a.y(), a.z());
            glVertex3f(c.x(), c.y(), c.z());
            glVertex3f(apex.x(), apex.y(), apex.z());
        }
        glEnd();
    } else {
        const bool ridgeAlongX = spanX >= spanZ;
        const float cx = (minX + maxX) * 0.5f;
        const float cz = (minZ + maxZ) * 0.5f;
        const float insetX = shape == "hipped" && ridgeAlongX ? spanX * 0.22f : 0.0f;
        const float insetZ = shape == "hipped" && !ridgeAlongX ? spanZ * 0.22f : 0.0f;

        const QVector3D p00(minX, h, minZ);
        const QVector3D p10(maxX, h, minZ);
        const QVector3D p11(maxX, h, maxZ);
        const QVector3D p01(minX, h, maxZ);

        if (ridgeAlongX) {
            const QVector3D r0(minX + insetX, h + roofHeight, cz);
            const QVector3D r1(maxX - insetX, h + roofHeight, cz);
            glBegin(GL_QUADS);
            glColor4f(r * 0.92f, g * 0.92f, b * 0.92f, 0.98f);
            glVertex3f(p00.x(), p00.y(), p00.z()); glVertex3f(p10.x(), p10.y(), p10.z());
            glVertex3f(r1.x(), r1.y(), r1.z()); glVertex3f(r0.x(), r0.y(), r0.z());
            glColor4f(qMin(r * 1.04f, 1.0f), qMin(g * 1.04f, 1.0f), qMin(b * 1.04f, 1.0f), 0.98f);
            glVertex3f(r0.x(), r0.y(), r0.z()); glVertex3f(r1.x(), r1.y(), r1.z());
            glVertex3f(p11.x(), p11.y(), p11.z()); glVertex3f(p01.x(), p01.y(), p01.z());
            glEnd();

            glBegin(GL_TRIANGLES);
            glColor4f(r * 0.86f, g * 0.86f, b * 0.86f, 0.98f);
            glVertex3f(p00.x(), p00.y(), p00.z()); glVertex3f(r0.x(), r0.y(), r0.z()); glVertex3f(p01.x(), p01.y(), p01.z());
            glColor4f(qMin(r * 1.0f, 1.0f), qMin(g * 1.0f, 1.0f), qMin(b * 1.0f, 1.0f), 0.98f);
            glVertex3f(p10.x(), p10.y(), p10.z()); glVertex3f(p11.x(), p11.y(), p11.z()); glVertex3f(r1.x(), r1.y(), r1.z());
            glEnd();
        } else {
            const QVector3D r0(cx, h + roofHeight, minZ + insetZ);
            const QVector3D r1(cx, h + roofHeight, maxZ - insetZ);
            glBegin(GL_QUADS);
            glColor4f(r * 0.92f, g * 0.92f, b * 0.92f, 0.98f);
            glVertex3f(p00.x(), p00.y(), p00.z()); glVertex3f(r0.x(), r0.y(), r0.z());
            glVertex3f(r1.x(), r1.y(), r1.z()); glVertex3f(p01.x(), p01.y(), p01.z());
            glColor4f(qMin(r * 1.04f, 1.0f), qMin(g * 1.04f, 1.0f), qMin(b * 1.04f, 1.0f), 0.98f);
            glVertex3f(r0.x(), r0.y(), r0.z()); glVertex3f(p10.x(), p10.y(), p10.z());
            glVertex3f(p11.x(), p11.y(), p11.z()); glVertex3f(r1.x(), r1.y(), r1.z());
            glEnd();

            glBegin(GL_TRIANGLES);
            glColor4f(r * 0.86f, g * 0.86f, b * 0.86f, 0.98f);
            glVertex3f(p00.x(), p00.y(), p00.z()); glVertex3f(p10.x(), p10.y(), p10.z()); glVertex3f(r0.x(), r0.y(), r0.z());
            glColor4f(qMin(r * 1.0f, 1.0f), qMin(g * 1.0f, 1.0f), qMin(b * 1.0f, 1.0f), 0.98f);
            glVertex3f(p01.x(), p01.y(), p01.z()); glVertex3f(r1.x(), r1.y(), r1.z()); glVertex3f(p11.x(), p11.y(), p11.z());
            glEnd();
        }
    }

    glLineWidth(1.0f);
    glColor4f(0.08f, 0.09f, 0.10f, 0.65f);
    glBegin(GL_LINE_LOOP);
    for (const QVector3D &p : topPoints) {
        glVertex3f(p.x(), p.y() + 0.03f, p.z());
    }
    glEnd();
}

void MapView3D::drawBuildingDetails(const BuildingData &building)
{
    const float h = building.height;
    if (building.footprint.size() < 3 || h < 5.0f) return;

    // 屋上の薄い縁取り
    glLineWidth(2.0f);
    glColor4f(0.03f, 0.035f, 0.04f, 0.75f);
    glBegin(GL_LINE_LOOP);
    for (const QVector2D &point : building.footprint) {
        const QVector3D p = buildingPointToLocal(point, h + 0.04f);
        glVertex3f(p.x(), p.y(), p.z());
    }
    glEnd();

    if (h < 8.0f) return;

    // 窓の簡易表現。高層ほど細かく、低層は控えめに出す。
    glBegin(GL_QUADS);
    for (int i = 0; i < building.footprint.size(); i++) {
        const QVector3D a = buildingPointToLocal(building.footprint[i], 0.0f);
        const QVector3D b = buildingPointToLocal(building.footprint[(i + 1) % building.footprint.size()], 0.0f);
        const QVector3D wall = b - a;
        const float wallLength = qSqrt(wall.x() * wall.x() + wall.z() * wall.z());
        if (wallLength < 8.0f) continue;

        QVector3D along(wall.x() / wallLength, 0.0f, wall.z() / wallLength);
        QVector3D normal(-along.z(), 0.0f, along.x());
        const float offset = 0.06f;
        const int columns = qBound(1, static_cast<int>(wallLength / 5.0f), 12);
        const int floors = qBound(1, static_cast<int>(h / 3.2f), 12);

        for (int floor = 0; floor < floors; floor++) {
            const float y0 = 2.0f + floor * 3.0f;
            if (y0 + 1.0f > h - 0.7f) continue;
            for (int col = 0; col < columns; col++) {
                if (((col + floor + i) % 4) == 0) continue;

                const float t = (col + 0.5f) / columns;
                const QVector3D center = a + along * (t * wallLength) + normal * offset;
                const float halfW = qMin(0.75f, wallLength / columns * 0.22f);
                const float y1 = y0 + 0.9f;
                const QColor lit = ((col + floor) % 3 == 0)
                    ? QColor(244, 210, 126)
                    : QColor(95, 130, 150);

                glColor4f(lit.redF(), lit.greenF(), lit.blueF(), 0.38f);
                const QVector3D left = center - along * halfW;
                const QVector3D right = center + along * halfW;
                glVertex3f(left.x(), y0, left.z());
                glVertex3f(right.x(), y0, right.z());
                glVertex3f(right.x(), y1, right.z());
                glVertex3f(left.x(), y1, left.z());
            }
        }
    }
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
    painter.setBrush(QColor(0, 0, 0, 115));
    painter.drawRoundedRect(10, height() - 72, 260, 62, 4, 4);
    painter.setPen(QColor(120, 120, 130));
    painter.drawText(18, height() - 55, m_buildingStatus);
    painter.drawText(18, height() - 38, "左ドラッグ: 回転 | 右ドラッグ: パン");
    painter.drawText(18, height() - 22, "ホイール: ズーム");

    drawBuildingLabels(painter);

    painter.end();
}

void MapView3D::drawBuildingLabels(QPainter &painter)
{
    QFont labelFont = painter.font();
    labelFont.setPixelSize(10);
    labelFont.setBold(true);
    painter.setFont(labelFont);
    const QFontMetrics metrics(labelFont);

    int drawn = 0;
    for (const BuildingData &building : m_buildings) {
        if (building.name.isEmpty() || building.footprint.size() < 3) {
            continue;
        }
        if (building.height < 10.0f && drawn >= 6) {
            continue;
        }

        const QVector3D anchor = buildingCenter(building) + QVector3D(0, 2.0f, 0);
        QPointF screen;
        if (!worldToScreen(anchor, screen)) {
            continue;
        }

        QString label = building.name;
        if (label.size() > 18) {
            label = label.left(17) + "...";
        }

        const int padX = 7;
        const int padY = 4;
        const QRect textRect = metrics.boundingRect(label);
        QRectF bg(screen.x() - textRect.width() / 2.0 - padX,
                  screen.y() - 28.0,
                  textRect.width() + padX * 2.0,
                  textRect.height() + padY * 2.0);

        painter.setPen(QPen(QColor(255, 230, 150, 160), 1));
        painter.drawLine(QPointF(screen.x(), screen.y() - 5.0),
                         QPointF(screen.x(), screen.y() + 12.0));
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(18, 20, 24, 175));
        painter.drawRoundedRect(bg, 3, 3);
        painter.setPen(QColor(235, 225, 190));
        painter.drawText(bg.adjusted(padX, padY - 1, -padX, -padY),
                         Qt::AlignCenter,
                         label);

        drawn++;
        if (drawn >= 12) {
            break;
        }
    }
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
    QVector3D relativeCursor(0, 0, 0);
    for (int i = 0; i < items.size(); i++) {
        WpData wp;
        if (items[i].coordinateMode == MissionItem::CoordinateMode::Relative) {
            relativeCursor += QVector3D(static_cast<float>(items[i].east_m),
                                         0.0f,
                                         static_cast<float>(-items[i].north_m));
            wp.pos = QVector3D(relativeCursor.x(),
                               static_cast<float>(items[i].altitude),
                               relativeCursor.z());
        } else {
            wp.pos = geoToLocal(items[i].latitude, items[i].longitude, items[i].altitude);
            relativeCursor = QVector3D(wp.pos.x(), 0.0f, wp.pos.z());
        }
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
