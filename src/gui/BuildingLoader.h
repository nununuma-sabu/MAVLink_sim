#ifndef BUILDINGLOADER_H
#define BUILDINGLOADER_H

#include <QColor>
#include <QByteArray>
#include <QString>
#include <QVector>
#include <QVector2D>

struct BuildingData {
    QString id;
    QString name;
    float height = 8.0f;
    QColor color = QColor(108, 122, 137);
    QVector<QVector2D> footprint;
};

struct GroundPathData {
    QString id;
    QString type;
    float width = 4.0f;
    QColor color = QColor(48, 48, 52);
    QVector<QVector2D> points;
};

class BuildingLoader
{
public:
    static QVector<BuildingData> loadFromJson(const QString &path);
    static QVector<BuildingData> loadFromJsonData(const QByteArray &data,
                                                  const QString &sourceName = QString());
    static QVector<GroundPathData> loadPathsFromJson(const QString &path);
    static QVector<GroundPathData> loadPathsFromJsonData(const QByteArray &data,
                                                         const QString &sourceName = QString());
};

#endif // BUILDINGLOADER_H
