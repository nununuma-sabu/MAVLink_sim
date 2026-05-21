#include "MapView3D.h"
#include "core/MissionManager.h"
#include "core/GeoUtils.h"
#include <QtMath>
#include <QPainter>
#include <QVector4D>
#include <QDebug>
#include <cstddef>

namespace {

struct PackedCityVertex {
    GLfloat x;
    GLfloat y;
    GLfloat z;
    GLfloat r;
    GLfloat g;
    GLfloat b;
    GLfloat a;
};

GLenum primitiveToGl(Map3DPrimitive primitive)
{
    switch (primitive) {
    case Map3DPrimitive::Lines:
        return GL_LINES;
    case Map3DPrimitive::Quads:
        return GL_QUADS;
    case Map3DPrimitive::Triangles:
        return GL_TRIANGLES;
    case Map3DPrimitive::TriangleFan:
        return GL_TRIANGLE_FAN;
    case Map3DPrimitive::LineLoop:
        return GL_LINE_LOOP;
    }
    return GL_TRIANGLES;
}

} // namespace

MapView3D::MapView3D(QWidget *parent)
    : QOpenGLWidget(parent)
{
    setMinimumSize(400, 300);
    setFocusPolicy(Qt::StrongFocus);

    // プロペラアニメーション用タイマー
    connect(&m_animTimer, &QTimer::timeout, [this]() {
        m_propellerAngle += 15.0f;
        if (m_propellerAngle >= 360.0f) m_propellerAngle -= 360.0f;
        m_droneModelVboDirty = true;
        update();
    });
    m_animTimer.start(33); // ~30fps

    m_buildingProvider = new BuildingProvider(this);
    connect(m_buildingProvider, &BuildingProvider::buildingsReady,
            this, [this](const QVector<BuildingData> &buildings, const QString &source) {
        m_buildings = buildings;
        m_buildingStatus = QString("建物データ: %1 (%2件)").arg(source).arg(buildings.size());
        rebuildStaticCityMesh();
        qDebug() << "[MapView3D] 建物データ適用:" << buildings.size() << "件 source:" << source;
        update();
    });
    connect(m_buildingProvider, &BuildingProvider::pathsReady,
            this, [this](const QVector<GroundPathData> &paths, const QString &source) {
        m_groundPaths = paths;
        rebuildStaticCityMesh();
        qDebug() << "[MapView3D] 地表パス適用:" << paths.size() << "件 source:" << source;
        update();
    });
    connect(m_buildingProvider, &BuildingProvider::statusMessage,
            this, [this](const QString &message) {
        m_buildingStatus = message;
        qDebug() << "[MapView3D]" << message;
        update();
    });
    setHome(m_homeLat, m_homeLon, 300, m_locationName);
}

MapView3D::~MapView3D()
{
    if (m_glInitialized) {
        makeCurrent();
        clearStaticCityVbos();
        clearDynamicVbos();
        delete m_staticCityProgram;
        m_staticCityProgram = nullptr;
        doneCurrent();
    }
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

QMatrix4x4 MapView3D::projectionMatrix() const
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
    return projection;
}

QMatrix4x4 MapView3D::viewMatrix() const
{
    const float radX = qDegreesToRadians(m_cameraAngleX);
    const float radY = qDegreesToRadians(m_cameraAngleY);
    const QVector3D eye(
        m_cameraTarget.x() + m_cameraDistance * qCos(radX) * qSin(radY),
        m_cameraTarget.y() + m_cameraDistance * qSin(radX),
        m_cameraTarget.z() + m_cameraDistance * qCos(radX) * qCos(radY));

    QMatrix4x4 view;
    view.lookAt(eye, m_cameraTarget, QVector3D(0, 1, 0));
    return view;
}

void MapView3D::setHome(double latitude, double longitude, int radiusMeters,
                        const QString &locationName,
                        float cameraDistance,
                        float cameraAngleX,
                        float cameraAngleY)
{
    m_homeLat = latitude;
    m_homeLon = longitude;
    m_homeSet = true;
    if (!locationName.isEmpty()) {
        m_locationName = locationName;
    }
    m_dronePos = QVector3D(0.0f, m_dronePos.y(), 0.0f);
    m_cameraDistance = cameraDistance;
    m_cameraAngleX = cameraAngleX;
    m_cameraAngleY = cameraAngleY;
    m_cameraTarget = QVector3D(0.0f, qMin(m_dronePos.y(), 24.0f), 0.0f);
    m_tracePath.clear();
    m_waypoints.clear();
    m_activeWpIndex = -1;
    m_traceVboDirty = true;
    m_waypointPathVboDirty = true;
    m_waypointMarkerVboDirty = true;
    m_buildings.clear();
    m_groundPaths.clear();
    rebuildStaticCityMesh();
    m_buildingStatus = "建物データ: OSM取得中";
    m_buildingProvider->loadForOrigin(m_homeLat, m_homeLon, radiusMeters);
    update();
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
    m_droneModelVboDirty = true;

    // トレース追加
    if (m_tracePath.isEmpty() ||
        (m_tracePath.last() - m_dronePos).length() > 0.1f) {
        m_tracePath.append(m_dronePos);
        if (m_tracePath.size() > MAX_TRACE_POINTS) {
            m_tracePath.removeFirst();
        }
        m_traceVboDirty = true;
    }

    // カメラをドローンに追従（スムーズ）
    m_cameraTarget = m_cameraTarget * 0.95f + m_dronePos * 0.05f;

    update();
}

void MapView3D::clearTrace()
{
    m_tracePath.clear();
    m_traceVboDirty = true;
    update();
}

// ============================================================
// OpenGL
// ============================================================

void MapView3D::initializeGL()
{
    initializeOpenGLFunctions();
    m_glInitialized = true;
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
    initializeStaticCityShader();
    uploadStaticCityMeshToGpu();
    uploadTraceToGpu();
    uploadWaypointPathToGpu();
    uploadWaypointMarkersToGpu();
    uploadDroneModelToGpu();
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
    drawStaticCityMesh();
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

void MapView3D::rebuildStaticCityMesh()
{
    m_staticCityMesh = Map3DMeshBuilder::build(m_buildings, m_groundPaths);
    m_staticCityVboDirty = true;
    if (m_glInitialized) {
        makeCurrent();
        uploadStaticCityMeshToGpu();
        doneCurrent();
    }
}

bool MapView3D::initializeStaticCityShader()
{
    if (m_staticCityProgram) {
        return true;
    }

    m_staticCityProgram = new QOpenGLShaderProgram(this);
    const char *vertexShader = R"(
        attribute vec3 a_position;
        attribute vec4 a_color;
        uniform mat4 u_mvp;
        uniform mat4 u_modelView;
        varying vec4 v_color;
        varying float v_eyeDistance;
        void main()
        {
            v_color = a_color;
            vec4 eyePos = u_modelView * vec4(a_position, 1.0);
            v_eyeDistance = length(eyePos.xyz);
            gl_Position = u_mvp * vec4(a_position, 1.0);
        }
    )";

    const char *fragmentShader = R"(
        varying vec4 v_color;
        varying float v_eyeDistance;
        uniform vec4 u_fogColor;
        uniform float u_fogStart;
        uniform float u_fogEnd;
        void main()
        {
            float fogFactor = clamp((u_fogEnd - v_eyeDistance) / (u_fogEnd - u_fogStart), 0.0, 1.0);
            vec4 color = vec4(mix(u_fogColor.rgb, v_color.rgb, fogFactor), v_color.a);
            gl_FragColor = color;
        }
    )";

    if (!m_staticCityProgram->addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShader) ||
        !m_staticCityProgram->addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShader) ||
        !m_staticCityProgram->link()) {
        qWarning() << "[MapView3D] 静的都市メッシュ用シェーダー初期化失敗:"
                   << m_staticCityProgram->log();
        delete m_staticCityProgram;
        m_staticCityProgram = nullptr;
        return false;
    }

    m_staticCityMvpLocation = m_staticCityProgram->uniformLocation("u_mvp");
    m_staticCityModelViewLocation = m_staticCityProgram->uniformLocation("u_modelView");
    m_staticCityFogColorLocation = m_staticCityProgram->uniformLocation("u_fogColor");
    m_staticCityFogStartLocation = m_staticCityProgram->uniformLocation("u_fogStart");
    m_staticCityFogEndLocation = m_staticCityProgram->uniformLocation("u_fogEnd");
    m_staticCityPositionLocation = m_staticCityProgram->attributeLocation("a_position");
    m_staticCityColorLocation = m_staticCityProgram->attributeLocation("a_color");
    return m_staticCityMvpLocation >= 0
        && m_staticCityModelViewLocation >= 0
        && m_staticCityFogColorLocation >= 0
        && m_staticCityFogStartLocation >= 0
        && m_staticCityFogEndLocation >= 0
        && m_staticCityPositionLocation >= 0
        && m_staticCityColorLocation >= 0;
}

void MapView3D::clearStaticCityVbos()
{
    for (const StaticVboBatch &batch : m_staticCityVboBatches) {
        if (batch.vertexBuffer != 0) {
            GLuint buffer = batch.vertexBuffer;
            glDeleteBuffers(1, &buffer);
        }
    }
    m_staticCityVboBatches.clear();
}

void MapView3D::uploadStaticCityMeshToGpu()
{
    if (!m_glInitialized || !m_staticCityVboDirty) {
        return;
    }

    clearStaticCityVbos();

    for (const Map3DMeshBatch &batch : m_staticCityMesh.batches) {
        if (batch.vertices.isEmpty()) continue;

        QVector<PackedCityVertex> packed;
        packed.reserve(batch.vertices.size());
        for (const Map3DVertex &vertex : batch.vertices) {
            const QColor &color = vertex.color;
            packed.append({
                vertex.position.x(),
                vertex.position.y(),
                vertex.position.z(),
                static_cast<GLfloat>(color.redF()),
                static_cast<GLfloat>(color.greenF()),
                static_cast<GLfloat>(color.blueF()),
                static_cast<GLfloat>(color.alphaF())
            });
        }

        GLuint buffer = 0;
        glGenBuffers(1, &buffer);
        glBindBuffer(GL_ARRAY_BUFFER, buffer);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(packed.size() * sizeof(PackedCityVertex)),
                     packed.constData(),
                     GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        StaticVboBatch vboBatch;
        vboBatch.primitive = batch.primitive;
        vboBatch.vertexBuffer = buffer;
        vboBatch.vertexCount = packed.size();
        vboBatch.lineWidth = batch.lineWidth;
        m_staticCityVboBatches.append(vboBatch);
    }

    m_staticCityVboDirty = false;
}

void MapView3D::clearDynamicVbos()
{
    if (m_traceVbo != 0) {
        glDeleteBuffers(1, &m_traceVbo);
        m_traceVbo = 0;
    }
    if (m_waypointPathVbo != 0) {
        glDeleteBuffers(1, &m_waypointPathVbo);
        m_waypointPathVbo = 0;
    }
    if (m_waypointMarkerTriangleVbo != 0) {
        glDeleteBuffers(1, &m_waypointMarkerTriangleVbo);
        m_waypointMarkerTriangleVbo = 0;
    }
    if (m_waypointMarkerLineVbo != 0) {
        glDeleteBuffers(1, &m_waypointMarkerLineVbo);
        m_waypointMarkerLineVbo = 0;
    }
    if (m_droneModelVbo != 0) {
        glDeleteBuffers(1, &m_droneModelVbo);
        m_droneModelVbo = 0;
    }
    m_traceVboCount = 0;
    m_waypointPathVboCount = 0;
    m_waypointMarkerTriangleVboCount = 0;
    m_waypointMarkerLineVboCount = 0;
    m_droneModelVboCount = 0;
}

void MapView3D::uploadTraceToGpu()
{
    if (!m_glInitialized || !m_traceVboDirty) {
        return;
    }

    if (m_tracePath.size() < 2) {
        m_traceVboCount = 0;
        m_traceVboDirty = false;
        return;
    }

    QVector<PackedCityVertex> packed;
    packed.reserve(m_tracePath.size());
    for (int i = 0; i < m_tracePath.size(); ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(m_tracePath.size());
        const QVector3D &point = m_tracePath[i];
        packed.append({
            point.x(),
            point.y(),
            point.z(),
            0.2f + 0.6f * t,
            0.4f + 0.4f * t,
            0.9f,
            0.3f + 0.7f * t
        });
    }

    if (m_traceVbo == 0) {
        glGenBuffers(1, &m_traceVbo);
    }
    glBindBuffer(GL_ARRAY_BUFFER, m_traceVbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(packed.size() * sizeof(PackedCityVertex)),
                 packed.constData(),
                 GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    m_traceVboCount = packed.size();
    m_traceVboDirty = false;
}

void MapView3D::uploadWaypointPathToGpu()
{
    if (!m_glInitialized || !m_waypointPathVboDirty) {
        return;
    }

    if (m_waypoints.isEmpty()) {
        m_waypointPathVboCount = 0;
        m_waypointPathVboDirty = false;
        return;
    }

    QVector<PackedCityVertex> packed;
    packed.reserve(m_waypoints.size());
    for (int i = 0; i < m_waypoints.size(); ++i) {
        const WpData &wp = m_waypoints[i];
        const bool isActive = (i == m_activeWpIndex);
        packed.append({
            wp.pos.x(),
            wp.pos.y(),
            wp.pos.z(),
            isActive ? 1.0f : 0.9f,
            isActive ? 0.78f : 0.3f,
            isActive ? 0.0f : 0.24f,
            isActive ? 0.9f : 0.7f
        });
    }

    if (m_waypointPathVbo == 0) {
        glGenBuffers(1, &m_waypointPathVbo);
    }
    glBindBuffer(GL_ARRAY_BUFFER, m_waypointPathVbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(packed.size() * sizeof(PackedCityVertex)),
                 packed.constData(),
                 GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    m_waypointPathVboCount = packed.size();
    m_waypointPathVboDirty = false;
}

void MapView3D::uploadWaypointMarkersToGpu()
{
    if (!m_glInitialized || !m_waypointMarkerVboDirty) {
        return;
    }

    if (m_waypoints.isEmpty()) {
        m_waypointMarkerTriangleVboCount = 0;
        m_waypointMarkerLineVboCount = 0;
        m_waypointMarkerVboDirty = false;
        return;
    }

    QVector<PackedCityVertex> triangles;
    QVector<PackedCityVertex> lines;
    triangles.reserve(m_waypoints.size() * 48);
    lines.reserve(m_waypoints.size() * 26);

    auto appendVertex = [](QVector<PackedCityVertex> &vertices,
                           const QVector3D &pos,
                           const QVector4D &color) {
        vertices.append({
            pos.x(),
            pos.y(),
            pos.z(),
            color.x(),
            color.y(),
            color.z(),
            color.w()
        });
    };

    for (int i = 0; i < m_waypoints.size(); ++i) {
        const WpData &wp = m_waypoints[i];
        const bool isActive = (i == m_activeWpIndex);
        const float r = isActive ? 1.2f : 0.8f;
        const float h = r * 0.7f;
        const QVector4D markerColor = isActive
            ? QVector4D(1.0f, 0.78f, 0.0f, 0.9f)
            : QVector4D(0.9f, 0.3f, 0.24f, 0.8f);
        const QVector4D guideColor(0.9f, 0.3f, 0.24f, 0.3f);
        const QVector4D groundColor(0.9f, 0.3f, 0.24f, 0.2f);

        QVector<QVector3D> ring;
        ring.reserve(9);
        for (int j = 0; j <= 8; ++j) {
            const float a = qDegreesToRadians(j * 45.0f);
            ring.append(wp.pos + QVector3D(r * qCos(a), 0.0f, r * qSin(a)));
        }

        const QVector3D top = wp.pos + QVector3D(0.0f, h, 0.0f);
        const QVector3D bottom = wp.pos + QVector3D(0.0f, -h, 0.0f);
        for (int j = 0; j < 8; ++j) {
            appendVertex(triangles, top, markerColor);
            appendVertex(triangles, ring[j], markerColor);
            appendVertex(triangles, ring[j + 1], markerColor);

            appendVertex(triangles, bottom, markerColor);
            appendVertex(triangles, ring[j + 1], markerColor);
            appendVertex(triangles, ring[j], markerColor);
        }

        appendVertex(lines, wp.pos, guideColor);
        appendVertex(lines, QVector3D(wp.pos.x(), 0.0f, wp.pos.z()), guideColor);

        const float groundRadius = r * 0.6f;
        QVector<QVector3D> groundRing;
        groundRing.reserve(12);
        for (int j = 0; j < 12; ++j) {
            const float a = qDegreesToRadians(j * 30.0f);
            groundRing.append(QVector3D(wp.pos.x() + groundRadius * qCos(a),
                                        0.02f,
                                        wp.pos.z() + groundRadius * qSin(a)));
        }
        for (int j = 0; j < groundRing.size(); ++j) {
            appendVertex(lines, groundRing[j], groundColor);
            appendVertex(lines, groundRing[(j + 1) % groundRing.size()], groundColor);
        }
    }

    if (m_waypointMarkerTriangleVbo == 0) {
        glGenBuffers(1, &m_waypointMarkerTriangleVbo);
    }
    glBindBuffer(GL_ARRAY_BUFFER, m_waypointMarkerTriangleVbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(triangles.size() * sizeof(PackedCityVertex)),
                 triangles.constData(),
                 GL_DYNAMIC_DRAW);

    if (m_waypointMarkerLineVbo == 0) {
        glGenBuffers(1, &m_waypointMarkerLineVbo);
    }
    glBindBuffer(GL_ARRAY_BUFFER, m_waypointMarkerLineVbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(lines.size() * sizeof(PackedCityVertex)),
                 lines.constData(),
                 GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    m_waypointMarkerTriangleVboCount = triangles.size();
    m_waypointMarkerLineVboCount = lines.size();
    m_waypointMarkerVboDirty = false;
}

void MapView3D::uploadDroneModelToGpu()
{
    if (!m_glInitialized || !m_droneModelVboDirty) {
        return;
    }

    QVector<PackedCityVertex> triangles;
    triangles.reserve(180);

    QMatrix4x4 model;
    model.translate(m_dronePos);
    model.rotate(-m_droneYaw, 0.0f, 1.0f, 0.0f);
    model.rotate(m_dronePitch, 1.0f, 0.0f, 0.0f);
    model.rotate(-m_droneRoll, 0.0f, 0.0f, 1.0f);

    auto transformed = [&model](const QVector3D &point) {
        return (model * QVector4D(point, 1.0f)).toVector3D();
    };
    auto appendVertex = [&triangles](const QVector3D &pos, const QVector4D &color) {
        triangles.append({
            pos.x(),
            pos.y(),
            pos.z(),
            color.x(),
            color.y(),
            color.z(),
            color.w()
        });
    };
    auto appendTriangle = [&](const QVector3D &a,
                              const QVector3D &b,
                              const QVector3D &c,
                              const QVector4D &color) {
        appendVertex(transformed(a), color);
        appendVertex(transformed(b), color);
        appendVertex(transformed(c), color);
    };
    auto appendQuad = [&](const QVector3D &a,
                          const QVector3D &b,
                          const QVector3D &c,
                          const QVector3D &d,
                          const QVector4D &color) {
        appendTriangle(a, b, c, color);
        appendTriangle(a, c, d, color);
    };
    auto appendBox = [&](float minX, float maxX,
                         float minY, float maxY,
                         float minZ, float maxZ,
                         const QVector4D &topColor,
                         const QVector4D &bottomColor,
                         const QVector4D &frontColor,
                         const QVector4D &sideColor) {
        appendQuad({minX, maxY, minZ}, {maxX, maxY, minZ},
                   {maxX, maxY, maxZ}, {minX, maxY, maxZ}, topColor);
        appendQuad({minX, minY, minZ}, {minX, minY, maxZ},
                   {maxX, minY, maxZ}, {maxX, minY, minZ}, bottomColor);
        appendQuad({minX, minY, minZ}, {maxX, minY, minZ},
                   {maxX, maxY, minZ}, {minX, maxY, minZ}, frontColor);
        appendQuad({minX, minY, maxZ}, {minX, maxY, maxZ},
                   {maxX, maxY, maxZ}, {maxX, minY, maxZ}, sideColor);
        appendQuad({minX, minY, minZ}, {minX, maxY, minZ},
                   {minX, maxY, maxZ}, {minX, minY, maxZ}, sideColor);
        appendQuad({maxX, minY, minZ}, {maxX, minY, maxZ},
                   {maxX, maxY, maxZ}, {maxX, maxY, minZ}, sideColor);
    };

    const float bodySize = 0.8f;
    const float bs = bodySize * 0.5f;
    const float bh = 0.15f;
    appendBox(-bs, bs, -bh, bh, -bs, bs,
              QVector4D(0.35f, 0.35f, 0.40f, 1.0f),
              QVector4D(0.25f, 0.25f, 0.30f, 1.0f),
              QVector4D(0.9f, 0.3f, 0.2f, 1.0f),
              QVector4D(0.3f, 0.3f, 0.35f, 1.0f));

    const float armLen = 1.5f;
    const float armW = 0.08f;
    const float armH = 0.05f;
    struct ArmDir { float dx; float dz; };
    const ArmDir arms[4] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};

    for (const ArmDir &arm : arms) {
        const float ex = arm.dx * armLen * 0.707f;
        const float ez = arm.dz * armLen * 0.707f;
        const QVector3D a(-armW * arm.dz, armH, -armW * (-arm.dx));
        const QVector3D b( armW * arm.dz, armH,  armW * (-arm.dx));
        const QVector3D c( armW * arm.dz + ex, armH,  armW * (-arm.dx) + ez);
        const QVector3D d(-armW * arm.dz + ex, armH, -armW * (-arm.dx) + ez);
        const QVector3D e(a.x(), -armH, a.z());
        const QVector3D f(b.x(), -armH, b.z());
        const QVector3D g(c.x(), -armH, c.z());
        const QVector3D h(d.x(), -armH, d.z());
        const QVector4D armColor(0.4f, 0.4f, 0.45f, 1.0f);
        appendQuad(a, b, c, d, armColor);
        appendQuad(e, h, g, f, armColor);

        const float motorRadius = 0.15f;
        const float motorTop = 0.2f;
        const QVector4D motorColor(0.5f, 0.5f, 0.55f, 1.0f);
        for (int i = 0; i < 12; ++i) {
            const float a0 = qDegreesToRadians(i * 30.0f);
            const float a1 = qDegreesToRadians((i + 1) * 30.0f);
            const QVector3D lower0(ex + static_cast<float>(motorRadius * qCos(a0)),
                                    armH,
                                    ez + static_cast<float>(motorRadius * qSin(a0)));
            const QVector3D lower1(ex + static_cast<float>(motorRadius * qCos(a1)),
                                    armH,
                                    ez + static_cast<float>(motorRadius * qSin(a1)));
            const QVector3D upper1(lower1.x(), motorTop, lower1.z());
            const QVector3D upper0(lower0.x(), motorTop, lower0.z());
            appendQuad(lower0, lower1, upper1, upper0,
                       motorColor);
        }

        QMatrix4x4 propeller;
        propeller.translate(ex, motorTop + 0.02f, ez);
        propeller.rotate(m_propellerAngle * arm.dx, 0.0f, 1.0f, 0.0f);
        auto propPoint = [&propeller](const QVector3D &point) {
            return (propeller * QVector4D(point, 1.0f)).toVector3D();
        };

        const float propR = 0.9f;
        const QVector4D propColor = arm.dz < 0
            ? QVector4D(0.9f, 0.3f, 0.2f, 0.6f)
            : QVector4D(0.2f, 0.5f, 0.9f, 0.6f);
        appendTriangle(propPoint({0.0f, 0.0f, 0.0f}),
                       propPoint({propR, 0.0f, 0.06f}),
                       propPoint({propR, 0.0f, -0.06f}),
                       propColor);
        appendTriangle(propPoint({0.0f, 0.0f, 0.0f}),
                       propPoint({-propR, 0.0f, 0.06f}),
                       propPoint({-propR, 0.0f, -0.06f}),
                       propColor);
    }

    const QVector4D frontLed(0.0f, 1.0f, 0.0f, 1.0f);
    const QVector4D rearLed(1.0f, 0.0f, 0.0f, 1.0f);
    appendBox(-0.08f, 0.08f, -bh - 0.01f, -bh + 0.01f, -bs - 0.16f, -bs - 0.08f,
              frontLed, frontLed, frontLed, frontLed);
    appendBox(-0.08f, 0.08f, -bh - 0.01f, -bh + 0.01f, bs + 0.08f, bs + 0.16f,
              rearLed, rearLed, rearLed, rearLed);

    if (m_droneModelVbo == 0) {
        glGenBuffers(1, &m_droneModelVbo);
    }
    glBindBuffer(GL_ARRAY_BUFFER, m_droneModelVbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(triangles.size() * sizeof(PackedCityVertex)),
                 triangles.constData(),
                 GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    m_droneModelVboCount = triangles.size();
    m_droneModelVboDirty = false;
}

void MapView3D::drawDynamicVbo(GLuint buffer, int vertexCount, GLenum primitive, float lineWidth)
{
    if (buffer == 0 || vertexCount <= 0) {
        return;
    }
    if (!m_staticCityProgram && !initializeStaticCityShader()) {
        return;
    }

    const QMatrix4x4 view = viewMatrix();
    const QMatrix4x4 mvp = projectionMatrix() * view;
    m_staticCityProgram->bind();
    m_staticCityProgram->setUniformValue(m_staticCityMvpLocation, mvp);
    m_staticCityProgram->setUniformValue(m_staticCityModelViewLocation, view);
    m_staticCityProgram->setUniformValue(m_staticCityFogColorLocation,
                                         QVector4D(0.42f, 0.52f, 0.62f, 1.0f));
    m_staticCityProgram->setUniformValue(m_staticCityFogStartLocation, 260.0f);
    m_staticCityProgram->setUniformValue(m_staticCityFogEndLocation, 760.0f);

    glLineWidth(lineWidth);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    m_staticCityProgram->enableAttributeArray(m_staticCityPositionLocation);
    m_staticCityProgram->setAttributeBuffer(m_staticCityPositionLocation,
                                            GL_FLOAT,
                                            offsetof(PackedCityVertex, x),
                                            3,
                                            sizeof(PackedCityVertex));
    m_staticCityProgram->enableAttributeArray(m_staticCityColorLocation);
    m_staticCityProgram->setAttributeBuffer(m_staticCityColorLocation,
                                            GL_FLOAT,
                                            offsetof(PackedCityVertex, r),
                                            4,
                                            sizeof(PackedCityVertex));
    glDrawArrays(primitive, 0, vertexCount);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    m_staticCityProgram->disableAttributeArray(m_staticCityColorLocation);
    m_staticCityProgram->disableAttributeArray(m_staticCityPositionLocation);
    m_staticCityProgram->release();
}

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

void MapView3D::drawStaticCityMesh()
{
    if (m_staticCityMesh.isEmpty()) {
        rebuildStaticCityMesh();
    }
    if (m_staticCityVboDirty) {
        uploadStaticCityMeshToGpu();
    }

    if (!m_staticCityProgram && !initializeStaticCityShader()) {
        return;
    }

    const QMatrix4x4 view = viewMatrix();
    const QMatrix4x4 mvp = projectionMatrix() * view;
    m_staticCityProgram->bind();
    m_staticCityProgram->setUniformValue(m_staticCityMvpLocation, mvp);
    m_staticCityProgram->setUniformValue(m_staticCityModelViewLocation, view);
    m_staticCityProgram->setUniformValue(m_staticCityFogColorLocation,
                                         QVector4D(0.42f, 0.52f, 0.62f, 1.0f));
    m_staticCityProgram->setUniformValue(m_staticCityFogStartLocation, 260.0f);
    m_staticCityProgram->setUniformValue(m_staticCityFogEndLocation, 760.0f);

    for (const StaticVboBatch &batch : m_staticCityVboBatches) {
        if (batch.vertexBuffer == 0 || batch.vertexCount <= 0) continue;

        if (batch.primitive == Map3DPrimitive::Lines ||
            batch.primitive == Map3DPrimitive::LineLoop) {
            glLineWidth(batch.lineWidth);
        }

        glBindBuffer(GL_ARRAY_BUFFER, batch.vertexBuffer);
        m_staticCityProgram->enableAttributeArray(m_staticCityPositionLocation);
        m_staticCityProgram->setAttributeBuffer(m_staticCityPositionLocation,
                                                GL_FLOAT,
                                                offsetof(PackedCityVertex, x),
                                                3,
                                                sizeof(PackedCityVertex));
        m_staticCityProgram->enableAttributeArray(m_staticCityColorLocation);
        m_staticCityProgram->setAttributeBuffer(m_staticCityColorLocation,
                                                GL_FLOAT,
                                                offsetof(PackedCityVertex, r),
                                                4,
                                                sizeof(PackedCityVertex));
        glDrawArrays(primitiveToGl(batch.primitive), 0, batch.vertexCount);
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    m_staticCityProgram->disableAttributeArray(m_staticCityColorLocation);
    m_staticCityProgram->disableAttributeArray(m_staticCityPositionLocation);
    m_staticCityProgram->release();
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
    if (m_droneModelVboDirty) {
        uploadDroneModelToGpu();
    }
    drawDynamicVbo(m_droneModelVbo, m_droneModelVboCount, GL_TRIANGLES, 1.0f);
}

void MapView3D::drawTrace()
{
    if (m_tracePath.size() < 2) return;

    if (m_traceVboDirty) {
        uploadTraceToGpu();
    }
    drawDynamicVbo(m_traceVbo, m_traceVboCount, GL_LINE_STRIP, 2.0f);
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

    // 地点・高度表示（左上）
    f.setPixelSize(14);
    painter.setFont(f);

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 120));
    painter.drawRoundedRect(10, 10, 190, 82, 6, 6);

    painter.setPen(QColor(150, 150, 150));
    f.setPixelSize(10);
    painter.setFont(f);
    painter.drawText(18, 28, "LOC");
    painter.drawText(18, 52, "ALT");
    painter.drawText(18, 76, "SPD");

    f.setPixelSize(16);
    f.setBold(true);
    painter.setFont(f);
    painter.setPen(QColor(0, 255, 100));
    painter.drawText(50, 30, m_locationName);
    painter.drawText(50, 54, QString::number(static_cast<double>(m_dronePos.y()), 'f', 1) + " m");

    float spd = 0;
    if (m_tracePath.size() >= 2) {
        QVector3D diff = m_tracePath.last() - m_tracePath[m_tracePath.size() - 2];
        spd = diff.length() * 30.0f; // 30fps * distance
    }
    painter.drawText(50, 78, QString::number(static_cast<double>(spd), 'f', 1) + " m/s");

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
    m_waypointPathVboDirty = true;
    m_waypointMarkerVboDirty = true;
    update();
}

void MapView3D::setActiveWaypoint(int index)
{
    m_activeWpIndex = index;
    m_waypointPathVboDirty = true;
    m_waypointMarkerVboDirty = true;
    update();
}

void MapView3D::drawWaypoints()
{
    if (m_waypoints.isEmpty()) return;

    // 経路ライン
    if (m_waypointPathVboDirty) {
        uploadWaypointPathToGpu();
    }
    drawDynamicVbo(m_waypointPathVbo, m_waypointPathVboCount, GL_LINE_STRIP, 2.0f);

    if (m_waypointMarkerVboDirty) {
        uploadWaypointMarkersToGpu();
    }
    drawDynamicVbo(m_waypointMarkerTriangleVbo,
                   m_waypointMarkerTriangleVboCount,
                   GL_TRIANGLES,
                   1.0f);
    drawDynamicVbo(m_waypointMarkerLineVbo,
                   m_waypointMarkerLineVboCount,
                   GL_LINES,
                   1.0f);
}
