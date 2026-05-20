#ifndef MAPLOCATION_H
#define MAPLOCATION_H

#include <QString>

struct MapLocation {
    QString id;
    QString name;
    QString category;
    double latitude = 0.0;
    double longitude = 0.0;
    int radiusMeters = 300;
    float cameraDistance = 95.0f;
    float cameraAngleX = 42.0f;
    float cameraAngleY = -45.0f;
};

#endif // MAPLOCATION_H
