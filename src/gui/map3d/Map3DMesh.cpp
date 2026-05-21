#include "Map3DMesh.h"
#include <QtMath>

namespace {

QVector3D pointToLocal(const QVector2D &point, float altitude)
{
    return QVector3D(point.x(), altitude, -point.y());
}

void addVertex(Map3DMeshBatch &batch, const QVector3D &position, const QColor &color)
{
    batch.vertices.append({position, color});
}

void addQuad(Map3DMeshBatch &batch, const QVector3D &a, const QVector3D &b,
             const QVector3D &c, const QVector3D &d, const QColor &color)
{
    addVertex(batch, a, color);
    addVertex(batch, b, color);
    addVertex(batch, c, color);
    addVertex(batch, d, color);
}

void addLine(Map3DMeshBatch &batch, const QVector3D &a, const QVector3D &b, const QColor &color)
{
    addVertex(batch, a, color);
    addVertex(batch, b, color);
}

float lightForWall(const QVector3D &a, const QVector3D &b, int wallIndex)
{
    const QVector3D wall = b - a;
    QVector3D normal(-wall.z(), 0.0f, wall.x());
    if (normal.lengthSquared() > 0.0f) {
        normal.normalize();
    }

    const QVector3D sunDir = QVector3D(-0.45f, 0.0f, -0.75f).normalized();
    const float direct = qMax(0.0f, QVector3D::dotProduct(normal, sunDir));
    const float variation = (wallIndex % 2 == 0) ? 0.04f : -0.03f;
    return qBound(0.45f, 0.58f + direct * 0.42f + variation, 1.08f);
}

QColor colorWithAlpha(const QColor &color, int alpha)
{
    QColor result = color;
    result.setAlpha(alpha);
    return result;
}

void addGround(Map3DStaticMesh &mesh)
{
    const float gridSize = 420.0f;
    const float cell = 28.0f;

    Map3DMeshBatch ground;
    ground.primitive = Map3DPrimitive::Quads;
    addQuad(ground,
            QVector3D(-gridSize, -0.01f, -gridSize),
            QVector3D( gridSize, -0.01f, -gridSize),
            QVector3D( gridSize, -0.01f,  gridSize),
            QVector3D(-gridSize, -0.01f,  gridSize),
            QColor(11, 20, 15, 245));

    for (float x = -gridSize; x < gridSize; x += cell) {
        for (float z = -gridSize; z < gridSize; z += cell) {
            const int ix = static_cast<int>((x + gridSize) / cell);
            const int iz = static_cast<int>((z + gridSize) / cell);
            const int pattern = qAbs((ix * 37 + iz * 17 + ix * iz * 3) % 11);

            QColor tone;
            int alpha = 56;
            if (pattern == 0 || pattern == 7) {
                tone = QColor("#263226");
                alpha = 76;
            } else if (pattern == 1 || pattern == 8) {
                tone = QColor("#394036");
                alpha = 56;
            } else if (pattern == 2) {
                tone = QColor("#4a4941");
                alpha = 46;
            } else {
                tone = QColor("#1f2c23");
                alpha = 41;
            }

            const float inset = 0.9f + static_cast<float>(pattern % 3) * 0.35f;
            addQuad(ground,
                    QVector3D(x + inset, 0.0f, z + inset),
                    QVector3D(x + cell - inset, 0.0f, z + inset),
                    QVector3D(x + cell - inset, 0.0f, z + cell - inset),
                    QVector3D(x + inset, 0.0f, z + cell - inset),
                    colorWithAlpha(tone, alpha));
        }
    }
    mesh.batches.append(ground);

    Map3DMeshBatch lines;
    lines.primitive = Map3DPrimitive::Lines;
    lines.lineWidth = 1.0f;
    for (float i = -gridSize; i <= gridSize; i += 10.0f) {
        const bool major = (fmod(qAbs(i), 50.0f) < 0.01f);
        const QColor gridColor = major
            ? QColor(61, 77, 71, 87)
            : QColor(43, 56, 51, 41);
        addLine(lines, QVector3D(i, 0.0f, -gridSize), QVector3D(i, 0.0f, gridSize), gridColor);
        addLine(lines, QVector3D(-gridSize, 0.0f, i), QVector3D(gridSize, 0.0f, i), gridColor);
    }
    for (float i = -gridSize; i <= gridSize; i += cell) {
        const QColor color(140, 148, 125, 26);
        addLine(lines, QVector3D(i, 0.006f, -gridSize), QVector3D(i, 0.006f, gridSize), color);
        addLine(lines, QVector3D(-gridSize, 0.006f, i), QVector3D(gridSize, 0.006f, i), color);
    }
    mesh.batches.append(lines);
}

void addRoadMarkings(Map3DStaticMesh &mesh, const GroundPathData &path, float halfWidth)
{
    if (path.width < 3.5f || path.type == "footway" || path.type == "path") {
        return;
    }

    Map3DMeshBatch edgeLines;
    edgeLines.primitive = Map3DPrimitive::Lines;
    edgeLines.lineWidth = 1.2f;

    Map3DMeshBatch markingQuads;
    markingQuads.primitive = Map3DPrimitive::Quads;

    for (int i = 0; i < path.points.size() - 1; i++) {
        const QVector3D a = pointToLocal(path.points[i], 0.065f);
        const QVector3D b = pointToLocal(path.points[i + 1], 0.065f);
        QVector3D dir = b - a;
        dir.setY(0.0f);
        const float len = dir.length();
        if (len < 0.01f) continue;
        dir /= len;

        const QVector3D normal(-dir.z(), 0.0f, dir.x());
        const float edgeOffset = qMax(halfWidth - 0.42f, 0.6f);
        const QColor edgeColor(224, 219, 189, 87);

        addLine(edgeLines, a + normal * edgeOffset, b + normal * edgeOffset, edgeColor);
        addLine(edgeLines, a - normal * edgeOffset, b - normal * edgeOffset, edgeColor);

        if (path.width >= 5.0f) {
            const float dashLength = qBound(2.6f, len * 0.08f, 5.0f);
            const float dashGap = dashLength * 1.25f;
            const float halfDashWidth = 0.12f;
            const QColor dashColor(240, 230, 168, 148);

            for (float t = dashGap * 0.5f; t < len - dashLength * 0.5f; t += dashLength + dashGap) {
                const QVector3D c0 = a + dir * t;
                const QVector3D c1 = a + dir * qMin(t + dashLength, len);
                addQuad(markingQuads,
                        c0 + normal * halfDashWidth,
                        c0 - normal * halfDashWidth,
                        c1 - normal * halfDashWidth,
                        c1 + normal * halfDashWidth,
                        dashColor);
            }
        }

        if (path.width >= 4.0f && (i == 0 || i == path.points.size() - 2 || len > 48.0f)) {
            const QVector3D center = a + dir * qBound(5.0f, len * 0.20f, 12.0f);
            const int stripeCount = qBound(4, static_cast<int>(halfWidth * 0.9f), 8);
            const float stripeLength = qMin(halfWidth * 1.72f, 9.0f);
            const float stripeWidth = 0.46f;
            const float gap = 0.52f;
            const float totalWidth = stripeCount * stripeWidth + (stripeCount - 1) * gap;
            const QVector3D start = center - dir * (totalWidth * 0.5f);
            const QColor crosswalkColor(230, 230, 209, 138);

            for (int s = 0; s < stripeCount; s++) {
                const QVector3D c = start + dir * (s * (stripeWidth + gap) + stripeWidth * 0.5f);
                addQuad(markingQuads,
                        c - normal * (stripeLength * 0.5f) - dir * (stripeWidth * 0.5f) + QVector3D(0, 0.012f, 0),
                        c + normal * (stripeLength * 0.5f) - dir * (stripeWidth * 0.5f) + QVector3D(0, 0.012f, 0),
                        c + normal * (stripeLength * 0.5f) + dir * (stripeWidth * 0.5f) + QVector3D(0, 0.012f, 0),
                        c - normal * (stripeLength * 0.5f) + dir * (stripeWidth * 0.5f) + QVector3D(0, 0.012f, 0),
                        crosswalkColor);
            }
        }
    }

    if (!edgeLines.vertices.isEmpty()) mesh.batches.append(edgeLines);
    if (!markingQuads.vertices.isEmpty()) mesh.batches.append(markingQuads);
}

void addRoad(Map3DStaticMesh &mesh, const GroundPathData &path)
{
    const float halfWidth = qBound(1.0f, path.width, 14.0f) * 0.5f;
    Map3DMeshBatch band;
    band.primitive = Map3DPrimitive::Quads;
    const QColor color = colorWithAlpha(path.color, 235);

    for (int i = 0; i < path.points.size() - 1; i++) {
        const QVector3D a = pointToLocal(path.points[i], 0.025f);
        const QVector3D b = pointToLocal(path.points[i + 1], 0.025f);
        QVector3D dir = b - a;
        dir.setY(0.0f);
        const float len = dir.length();
        if (len < 0.01f) continue;
        dir /= len;

        const QVector3D normal(-dir.z(), 0.0f, dir.x());
        addQuad(band, a + normal * halfWidth, a - normal * halfWidth,
                b - normal * halfWidth, b + normal * halfWidth, color);
    }

    if (!band.vertices.isEmpty()) mesh.batches.append(band);
    addRoadMarkings(mesh, path, halfWidth);
}

void addRailway(Map3DStaticMesh &mesh, const GroundPathData &path)
{
    Map3DMeshBatch bedBatch;
    bedBatch.primitive = Map3DPrimitive::Quads;
    Map3DMeshBatch railBatch;
    railBatch.primitive = Map3DPrimitive::Lines;
    railBatch.lineWidth = 2.0f;
    Map3DMeshBatch sleeperBatch;
    sleeperBatch.primitive = Map3DPrimitive::Lines;
    sleeperBatch.lineWidth = 1.5f;

    const float railOffset = qMax(1.0f, path.width * 0.22f);
    const QColor bedColor(path.color.redF() * 255 * 0.75f,
                          path.color.greenF() * 255 * 0.75f,
                          path.color.blueF() * 255 * 0.75f,
                          209);
    const QColor railColor(194, 184, 168, 242);
    const QColor sleeperColor(107, 87, 71, 217);

    for (int i = 0; i < path.points.size() - 1; i++) {
        const QVector3D a = pointToLocal(path.points[i], 0.03f);
        const QVector3D b = pointToLocal(path.points[i + 1], 0.03f);
        QVector3D dir = b - a;
        dir.setY(0.0f);
        const float len = dir.length();
        if (len < 0.01f) continue;
        dir /= len;

        const QVector3D normal(-dir.z(), 0.0f, dir.x());
        const float bedHalfWidth = qBound(2.5f, path.width * 0.65f, 5.0f);
        addQuad(bedBatch, a + normal * bedHalfWidth, a - normal * bedHalfWidth,
                b - normal * bedHalfWidth, b + normal * bedHalfWidth, bedColor);

        addLine(railBatch, a + normal * railOffset + QVector3D(0, 0.04f, 0),
                b + normal * railOffset + QVector3D(0, 0.04f, 0), railColor);
        addLine(railBatch, a - normal * railOffset + QVector3D(0, 0.04f, 0),
                b - normal * railOffset + QVector3D(0, 0.04f, 0), railColor);

        const int sleeperCount = qBound(1, static_cast<int>(len / 5.0f), 24);
        for (int s = 0; s <= sleeperCount; s++) {
            const QVector3D c = a + dir * (len * s / sleeperCount);
            addLine(sleeperBatch,
                    c + normal * (railOffset + 0.45f) + QVector3D(0, 0.055f, 0),
                    c - normal * (railOffset + 0.45f) + QVector3D(0, 0.055f, 0),
                    sleeperColor);
        }
    }

    if (!bedBatch.vertices.isEmpty()) mesh.batches.append(bedBatch);
    if (!railBatch.vertices.isEmpty()) mesh.batches.append(railBatch);
    if (!sleeperBatch.vertices.isEmpty()) mesh.batches.append(sleeperBatch);
}

void addFlatRoof(Map3DStaticMesh &mesh, const BuildingData &building,
                 const QVector<QVector3D> &topPoints, const QVector3D &center,
                 const QColor &roofColor)
{
    Map3DMeshBatch batch;
    batch.primitive = Map3DPrimitive::TriangleFan;
    addVertex(batch, QVector3D(center.x(), building.height, center.z()), roofColor);
    for (int i = 0; i <= topPoints.size(); i++) {
        addVertex(batch, topPoints[i % topPoints.size()], roofColor);
    }
    mesh.batches.append(batch);
}

void addRoof(Map3DStaticMesh &mesh, const BuildingData &building)
{
    const float h = building.height;
    const QColor roof = building.roofColor.isValid()
        ? building.roofColor
        : building.color.lighter(115);
    const QString shape = building.roofShape.toLower();
    QVector<QVector3D> topPoints;
    QVector3D center(0, 0, 0);
    float minX = 0.0f, maxX = 0.0f, minZ = 0.0f, maxZ = 0.0f;

    for (const QVector2D &point : building.footprint) {
        const QVector3D p = pointToLocal(point, h);
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

    const QColor roofColor(
        qMin(static_cast<int>(roof.red() * 1.10f + 8), 255),
        qMin(static_cast<int>(roof.green() * 1.10f + 8), 255),
        qMin(static_cast<int>(roof.blue() * 1.10f + 8), 255),
        245);

    const float spanX = maxX - minX;
    const float spanZ = maxZ - minZ;
    const bool boxRoof = topPoints.size() == 4 && spanX > 3.0f && spanZ > 3.0f;
    const float roofHeight = qBound(1.2f, h * 0.16f, 6.0f);

    if (!boxRoof || shape == "flat") {
        addFlatRoof(mesh, building, topPoints, center, roofColor);
    } else if (shape == "pyramidal") {
        Map3DMeshBatch batch;
        batch.primitive = Map3DPrimitive::Triangles;
        const QVector3D apex(center.x(), h + roofHeight, center.z());
        for (int i = 0; i < topPoints.size(); i++) {
            const float shade = 0.92f + 0.10f * (i % 2);
            const QColor color(qMin(static_cast<int>(roof.red() * shade), 255),
                               qMin(static_cast<int>(roof.green() * shade), 255),
                               qMin(static_cast<int>(roof.blue() * shade), 255),
                               250);
            addVertex(batch, topPoints[i], color);
            addVertex(batch, topPoints[(i + 1) % topPoints.size()], color);
            addVertex(batch, apex, color);
        }
        mesh.batches.append(batch);
    } else {
        Map3DMeshBatch quads;
        quads.primitive = Map3DPrimitive::Quads;
        Map3DMeshBatch triangles;
        triangles.primitive = Map3DPrimitive::Triangles;

        const bool ridgeAlongX = spanX >= spanZ;
        const float cx = (minX + maxX) * 0.5f;
        const float cz = (minZ + maxZ) * 0.5f;
        const float insetX = shape == "hipped" && ridgeAlongX ? spanX * 0.22f : 0.0f;
        const float insetZ = shape == "hipped" && !ridgeAlongX ? spanZ * 0.22f : 0.0f;
        const QVector3D p00(minX, h, minZ), p10(maxX, h, minZ), p11(maxX, h, maxZ), p01(minX, h, maxZ);
        const QColor shadeA(qMin(static_cast<int>(roof.red() * 0.92f), 255),
                            qMin(static_cast<int>(roof.green() * 0.92f), 255),
                            qMin(static_cast<int>(roof.blue() * 0.92f), 255), 250);
        const QColor shadeB(qMin(static_cast<int>(roof.red() * 1.04f), 255),
                            qMin(static_cast<int>(roof.green() * 1.04f), 255),
                            qMin(static_cast<int>(roof.blue() * 1.04f), 255), 250);
        const QColor shadeC(qMin(static_cast<int>(roof.red() * 0.86f), 255),
                            qMin(static_cast<int>(roof.green() * 0.86f), 255),
                            qMin(static_cast<int>(roof.blue() * 0.86f), 255), 250);

        if (ridgeAlongX) {
            const QVector3D r0(minX + insetX, h + roofHeight, cz);
            const QVector3D r1(maxX - insetX, h + roofHeight, cz);
            addQuad(quads, p00, p10, r1, r0, shadeA);
            addQuad(quads, r0, r1, p11, p01, shadeB);
            addVertex(triangles, p00, shadeC); addVertex(triangles, r0, shadeC); addVertex(triangles, p01, shadeC);
            addVertex(triangles, p10, roofColor); addVertex(triangles, p11, roofColor); addVertex(triangles, r1, roofColor);
        } else {
            const QVector3D r0(cx, h + roofHeight, minZ + insetZ);
            const QVector3D r1(cx, h + roofHeight, maxZ - insetZ);
            addQuad(quads, p00, r0, r1, p01, shadeA);
            addQuad(quads, r0, p10, p11, r1, shadeB);
            addVertex(triangles, p00, shadeC); addVertex(triangles, p10, shadeC); addVertex(triangles, r0, shadeC);
            addVertex(triangles, p01, roofColor); addVertex(triangles, r1, roofColor); addVertex(triangles, p11, roofColor);
        }

        mesh.batches.append(quads);
        mesh.batches.append(triangles);
    }

    Map3DMeshBatch outline;
    outline.primitive = Map3DPrimitive::LineLoop;
    outline.lineWidth = 1.0f;
    for (const QVector3D &p : topPoints) {
        addVertex(outline, QVector3D(p.x(), p.y() + 0.03f, p.z()), QColor(20, 23, 26, 166));
    }
    mesh.batches.append(outline);
}

void addBuildingDetails(Map3DStaticMesh &mesh, const BuildingData &building)
{
    const float h = building.height;
    if (building.footprint.size() < 3 || h < 5.0f) return;

    Map3DMeshBatch roofEdge;
    roofEdge.primitive = Map3DPrimitive::LineLoop;
    roofEdge.lineWidth = 2.0f;
    for (const QVector2D &point : building.footprint) {
        addVertex(roofEdge, pointToLocal(point, h + 0.04f), QColor(8, 9, 10, 191));
    }
    mesh.batches.append(roofEdge);

    if (h < 8.0f) return;

    Map3DMeshBatch windows;
    windows.primitive = Map3DPrimitive::Quads;
    for (int i = 0; i < building.footprint.size(); i++) {
        const QVector3D a = pointToLocal(building.footprint[i], 0.0f);
        const QVector3D b = pointToLocal(building.footprint[(i + 1) % building.footprint.size()], 0.0f);
        const QVector3D wall = b - a;
        const float wallLength = qSqrt(wall.x() * wall.x() + wall.z() * wall.z());
        if (wallLength < 8.0f) continue;

        const QVector3D along(wall.x() / wallLength, 0.0f, wall.z() / wallLength);
        const QVector3D normal(-along.z(), 0.0f, along.x());
        const int columns = qBound(1, static_cast<int>(wallLength / 5.0f), 12);
        const int floors = qBound(1, static_cast<int>(h / 3.2f), 12);

        for (int floor = 0; floor < floors; floor++) {
            const float y0 = 2.0f + floor * 3.0f;
            if (y0 + 1.0f > h - 0.7f) continue;
            for (int col = 0; col < columns; col++) {
                if (((col + floor + i) % 4) == 0) continue;
                const float t = (col + 0.5f) / columns;
                const QVector3D center = a + along * (t * wallLength) + normal * 0.06f;
                const float halfW = qMin(0.75f, wallLength / columns * 0.22f);
                const float y1 = y0 + 0.9f;
                const QColor lit = ((col + floor) % 3 == 0)
                    ? QColor(244, 210, 126, 97)
                    : QColor(95, 130, 150, 97);
                addQuad(windows,
                        center - along * halfW + QVector3D(0, y0, 0),
                        center + along * halfW + QVector3D(0, y0, 0),
                        center + along * halfW + QVector3D(0, y1, 0),
                        center - along * halfW + QVector3D(0, y1, 0),
                        lit);
            }
        }
    }
    if (!windows.vertices.isEmpty()) mesh.batches.append(windows);
}

void addBuilding(Map3DStaticMesh &mesh, const BuildingData &building)
{
    if (building.footprint.size() < 3) return;
    Map3DMeshBatch walls;
    walls.primitive = Map3DPrimitive::Quads;

    for (int i = 0; i < building.footprint.size(); i++) {
        const QVector2D &a = building.footprint[i];
        const QVector2D &b = building.footprint[(i + 1) % building.footprint.size()];
        const QVector3D a0 = pointToLocal(a, 0.0f);
        const QVector3D b0 = pointToLocal(b, 0.0f);
        const QVector3D b1 = pointToLocal(b, building.height);
        const QVector3D a1 = pointToLocal(a, building.height);
        const float shade = lightForWall(a0, b0, i);
        const QColor color(qMin(static_cast<int>(building.color.red() * shade), 255),
                           qMin(static_cast<int>(building.color.green() * shade), 255),
                           qMin(static_cast<int>(building.color.blue() * shade), 255),
                           240);
        addQuad(walls, a0, b0, b1, a1, color);
    }
    mesh.batches.append(walls);
    addRoof(mesh, building);
    addBuildingDetails(mesh, building);
}

} // namespace

Map3DStaticMesh Map3DMeshBuilder::build(const QVector<BuildingData> &buildings,
                                        const QVector<GroundPathData> &paths)
{
    Map3DStaticMesh mesh;
    addGround(mesh);

    for (const GroundPathData &path : paths) {
        if (path.points.size() < 2) continue;
        if (path.type == "railway") {
            addRailway(mesh, path);
        } else {
            addRoad(mesh, path);
        }
    }

    for (const BuildingData &building : buildings) {
        addBuilding(mesh, building);
    }

    return mesh;
}
