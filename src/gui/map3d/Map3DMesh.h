#ifndef MAP3DMESH_H
#define MAP3DMESH_H

#include "BuildingLoader.h"
#include <QColor>
#include <QVector>
#include <QVector3D>

struct Map3DVertex {
    QVector3D position;
    QColor color;
};

enum class Map3DPrimitive {
    Lines,
    Quads,
    Triangles,
    TriangleFan,
    LineLoop
};

struct Map3DMeshBatch {
    Map3DPrimitive primitive = Map3DPrimitive::Triangles;
    float lineWidth = 1.0f;
    QVector<Map3DVertex> vertices;
};

struct Map3DStaticMesh {
    QVector<Map3DMeshBatch> batches;

    void clear() { batches.clear(); }
    bool isEmpty() const { return batches.isEmpty(); }
};

class Map3DMeshBuilder
{
public:
    static Map3DStaticMesh build(const QVector<BuildingData> &buildings,
                                 const QVector<GroundPathData> &paths);
};

#endif // MAP3DMESH_H
