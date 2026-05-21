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
