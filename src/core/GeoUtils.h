#ifndef GEOUTILS_H
#define GEOUTILS_H

#include <QtMath>

namespace Geo {

static constexpr double NerimaStationLat = 35.7378;
static constexpr double NerimaStationLon = 139.6542;
static constexpr double MetersPerDegreeLatitude = 111320.0;

struct GeoPoint {
    double latitude = 0.0;
    double longitude = 0.0;
};

struct LocalOffset {
    double north = 0.0;
    double east = 0.0;
};

inline GeoPoint relativeToGeo(double originLat, double originLon,
                              double northMeters, double eastMeters)
{
    const double lat = originLat + northMeters / MetersPerDegreeLatitude;
    const double lonScale = MetersPerDegreeLatitude * qCos(qDegreesToRadians(originLat));
    const double lon = originLon + (lonScale != 0.0 ? eastMeters / lonScale : 0.0);
    return {lat, lon};
}

inline LocalOffset geoToRelative(double originLat, double originLon,
                                 double latitude, double longitude)
{
    const double north = (latitude - originLat) * MetersPerDegreeLatitude;
    const double east = (longitude - originLon) * MetersPerDegreeLatitude *
                        qCos(qDegreesToRadians(originLat));
    return {north, east};
}

} // namespace Geo

#endif // GEOUTILS_H
