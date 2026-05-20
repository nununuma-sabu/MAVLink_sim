#include "MapLocationLoader.h"
#include "GeoUtils.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QDebug>

QVector<MapLocation> MapLocationLoader::loadFromJson(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "[MapLocationLoader] 地点JSONを開けません:" << path << file.errorString();
        return defaultLocations();
    }

    const QVector<MapLocation> locations = loadFromJsonData(file.readAll(), path);
    return locations.isEmpty() ? defaultLocations() : locations;
}

QVector<MapLocation> MapLocationLoader::loadFromJsonData(const QByteArray &data,
                                                         const QString &sourceName)
{
    QVector<MapLocation> locations;

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "[MapLocationLoader] JSON解析失敗:" << sourceName << parseError.errorString();
        return locations;
    }

    const QJsonArray array = doc.object().value("locations").toArray();
    for (const QJsonValue &value : array) {
        const QJsonObject obj = value.toObject();
        MapLocation location;
        location.id = obj.value("id").toString();
        location.name = obj.value("name").toString();
        location.category = obj.value("category").toString();
        location.latitude = obj.value("latitude").toDouble();
        location.longitude = obj.value("longitude").toDouble();
        location.radiusMeters = obj.value("radius_m").toInt(300);
        const QJsonObject camera = obj.value("camera").toObject();
        location.cameraDistance = static_cast<float>(
            camera.value("distance").toDouble(location.cameraDistance));
        location.cameraAngleX = static_cast<float>(
            camera.value("angle_x").toDouble(location.cameraAngleX));
        location.cameraAngleY = static_cast<float>(
            camera.value("angle_y").toDouble(location.cameraAngleY));

        if (location.id.isEmpty() || location.name.isEmpty()
            || location.latitude == 0.0 || location.longitude == 0.0) {
            continue;
        }

        location.radiusMeters = qBound(150, location.radiusMeters, 600);
        location.cameraDistance = qBound(40.0f, location.cameraDistance, 260.0f);
        location.cameraAngleX = qBound(15.0f, location.cameraAngleX, 70.0f);
        locations.append(location);
    }

    qDebug() << "[MapLocationLoader] 地点読み込み:" << locations.size() << "件" << sourceName;
    return locations;
}

QVector<MapLocation> MapLocationLoader::defaultLocations()
{
    return {
        {"nerima_station", "練馬駅", "station", Geo::NerimaStationLat, Geo::NerimaStationLon, 300, 95.0f, 42.0f, -45.0f}
    };
}
