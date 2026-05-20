#ifndef MAPLOCATIONLOADER_H
#define MAPLOCATIONLOADER_H

#include "MapLocation.h"
#include <QByteArray>
#include <QString>
#include <QVector>

class MapLocationLoader
{
public:
    static QVector<MapLocation> loadFromJson(const QString &path);
    static QVector<MapLocation> loadFromJsonData(const QByteArray &data,
                                                 const QString &sourceName = QString());
    static QVector<MapLocation> defaultLocations();
};

#endif // MAPLOCATIONLOADER_H
