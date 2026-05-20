#ifndef BUILDINGLOADER_H
#define BUILDINGLOADER_H

#include <QColor>
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

class BuildingLoader
{
public:
    static QVector<BuildingData> loadFromJson(const QString &path);
};

#endif // BUILDINGLOADER_H
