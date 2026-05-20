#include "BuildingProvider.h"
#include "core/GeoUtils.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QDebug>

namespace {

float parseMeters(const QString &value, float fallback)
{
    static const QRegularExpression numberPattern(R"((-?\d+(?:\.\d+)?))");
    const QRegularExpressionMatch match = numberPattern.match(value);
    if (!match.hasMatch()) {
        return fallback;
    }
    bool ok = false;
    const float parsed = match.captured(1).toFloat(&ok);
    return ok && parsed > 0.0f ? parsed : fallback;
}

float heightFromTags(const QJsonObject &tags)
{
    if (tags.contains("height")) {
        return parseMeters(tags.value("height").toString(), 8.0f);
    }
    if (tags.contains("building:levels")) {
        return parseMeters(tags.value("building:levels").toString(), 0.0f) * 3.0f;
    }
    if (tags.value("building").toString() == "apartments") {
        return 15.0f;
    }
    if (tags.value("building").toString() == "commercial") {
        return 12.0f;
    }
    return 8.0f;
}

QColor colorForIndex(int index)
{
    static const QColor colors[] = {
        QColor("#7c8a96"),
        QColor("#8f7f6c"),
        QColor("#6f8890"),
        QColor("#899174"),
        QColor("#817c91"),
        QColor("#927b76")
    };
    return colors[index % (sizeof(colors) / sizeof(colors[0]))];
}

float pathWidthFromTags(const QJsonObject &tags)
{
    if (tags.contains("railway")) {
        return 5.0f;
    }

    const QString highway = tags.value("highway").toString();
    if (highway == "primary" || highway == "secondary") {
        return 9.0f;
    }
    if (highway == "tertiary" || highway == "residential") {
        return 6.0f;
    }
    if (highway == "service") {
        return 4.0f;
    }
    if (highway == "footway" || highway == "path" || highway == "pedestrian") {
        return 2.2f;
    }
    return 4.5f;
}

QColor pathColorFromTags(const QJsonObject &tags)
{
    if (tags.contains("railway")) {
        return QColor("#5c4b64");
    }

    const QString highway = tags.value("highway").toString();
    if (highway == "footway" || highway == "path" || highway == "pedestrian") {
        return QColor("#6f7465");
    }
    if (highway == "service") {
        return QColor("#3d4140");
    }
    return QColor("#303236");
}

} // namespace

BuildingProvider::BuildingProvider(QObject *parent)
    : QObject(parent)
{
}

void BuildingProvider::loadForOrigin(double latitude, double longitude, int radiusMeters)
{
    const QString path = cachePath(latitude, longitude, radiusMeters);
    if (QFile::exists(path)) {
        const QVector<BuildingData> cached = BuildingLoader::loadFromJson(path);
        const QVector<GroundPathData> cachedPaths = BuildingLoader::loadPathsFromJson(path);
        if (!cached.isEmpty()) {
            emit statusMessage(QString("建物データ: キャッシュ (%1件 / パス%2件)")
                               .arg(cached.size())
                               .arg(cachedPaths.size()));
            emit pathsReady(cachedPaths, "cache");
            emit buildingsReady(cached, "cache");
            return;
        }
    }

    emit statusMessage("建物データ: OSM取得中");
    const QVector<BuildingData> preview = fallbackBuildings();
    const QVector<GroundPathData> previewPaths = fallbackPaths();
    if (!preview.isEmpty()) {
        emit pathsReady(previewPaths, "sample-preview");
        emit buildingsReady(preview, "sample-preview");
    }

    QNetworkRequest request(QUrl("https://overpass-api.de/api/interpreter"));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      "application/x-www-form-urlencoded; charset=utf-8");
    request.setRawHeader("User-Agent", "MAVLink_sim/1.0 (building visualization)");

    const QByteArray body = "data=" + QUrl::toPercentEncoding(
        buildOverpassQuery(latitude, longitude, radiusMeters));
    QNetworkReply *reply = m_network.post(request, body);

    QTimer::singleShot(12000, reply, [reply]() {
        if (reply->isRunning()) {
            reply->abort();
        }
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, latitude, longitude, radiusMeters, path]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "[BuildingProvider] OSM取得失敗:"
                       << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()
                       << reply->errorString();
            const auto fallback = fallbackBuildings();
            const auto fallbackPathData = fallbackPaths();
            emit statusMessage(QString("建物データ: サンプル (%1件 / パス%2件 / OSM取得失敗)")
                               .arg(fallback.size())
                               .arg(fallbackPathData.size()));
            emit pathsReady(fallbackPathData, "sample");
            emit buildingsReady(fallback, "sample");
            return;
        }

        const QByteArray body = reply->readAll();
        const QVector<BuildingData> buildings = parseOverpassJson(body, latitude, longitude);
        const QVector<GroundPathData> paths = parseOverpassPaths(body, latitude, longitude);
        if (buildings.isEmpty()) {
            qWarning() << "[BuildingProvider] OSM建物データが空です";
            const auto fallback = fallbackBuildings();
            const auto fallbackPathData = fallbackPaths();
            emit statusMessage(QString("建物データ: サンプル (%1件 / パス%2件)")
                               .arg(fallback.size())
                               .arg(fallbackPathData.size()));
            emit pathsReady(fallbackPathData, "sample");
            emit buildingsReady(fallback, "sample");
            return;
        }

        QFile cacheFile(path);
        QDir().mkpath(QFileInfo(path).absolutePath());
        if (cacheFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            cacheFile.write(toCacheJson(buildings, paths, latitude, longitude, radiusMeters));
        }

        emit statusMessage(QString("建物データ: OSM取得 (%1件 / パス%2件)")
                           .arg(buildings.size())
                           .arg(paths.size()));
        emit pathsReady(paths, "osm");
        emit buildingsReady(buildings, "osm");
    });
}

QString BuildingProvider::cachePath(double latitude, double longitude, int radiusMeters) const
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (base.isEmpty()) {
        base = QDir::homePath() + "/.mavlink_sim";
    }

    const QString key = QString("%1_%2_%3")
        .arg(latitude, 0, 'f', 5)
        .arg(longitude, 0, 'f', 5)
        .arg(radiusMeters);
    return QDir(base).filePath("building_cache/" + key + "_v2.json");
}

QString BuildingProvider::buildOverpassQuery(double latitude, double longitude, int radiusMeters) const
{
    return QString(
        "[out:json][timeout:10];"
        "("
        "way[\"building\"](around:%1,%2,%3);"
        "way[\"highway\"](around:%1,%2,%3);"
        "way[\"railway\"](around:%1,%2,%3);"
        ");"
        "out body;"
        ">;"
        "out skel qt;")
        .arg(radiusMeters)
        .arg(latitude, 0, 'f', 7)
        .arg(longitude, 0, 'f', 7);
}

QVector<BuildingData> BuildingProvider::parseOverpassJson(const QByteArray &data,
                                                          double originLat,
                                                          double originLon) const
{
    QVector<BuildingData> buildings;

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "[BuildingProvider] Overpass JSON解析失敗:" << parseError.errorString();
        return buildings;
    }

    QHash<qint64, Geo::GeoPoint> nodes;
    QVector<QJsonObject> ways;

    const QJsonArray elements = doc.object().value("elements").toArray();
    for (const QJsonValue &value : elements) {
        const QJsonObject obj = value.toObject();
        const QString type = obj.value("type").toString();
        if (type == "node") {
            nodes.insert(static_cast<qint64>(obj.value("id").toDouble()),
                         {obj.value("lat").toDouble(), obj.value("lon").toDouble()});
        } else if (type == "way" && obj.value("tags").toObject().contains("building")) {
            ways.append(obj);
        }
    }

    for (const QJsonObject &way : ways) {
        const QJsonArray nodeIds = way.value("nodes").toArray();
        if (nodeIds.size() < 4) {
            continue;
        }

        BuildingData building;
        const QJsonObject tags = way.value("tags").toObject();
        building.id = QString("osm_way_%1").arg(static_cast<qint64>(way.value("id").toDouble()));
        building.name = tags.value("name").toString();
        building.height = heightFromTags(tags);
        building.color = colorForIndex(buildings.size());

        for (int i = 0; i < nodeIds.size(); i++) {
            const qint64 nodeId = static_cast<qint64>(nodeIds[i].toDouble());
            if (!nodes.contains(nodeId)) {
                building.footprint.clear();
                break;
            }
            if (i == nodeIds.size() - 1 && nodeIds.first().toDouble() == nodeIds.last().toDouble()) {
                continue;
            }
            const auto geo = nodes.value(nodeId);
            const auto offset = Geo::geoToRelative(originLat, originLon,
                                                   geo.latitude, geo.longitude);
            building.footprint.append(QVector2D(static_cast<float>(offset.east),
                                                static_cast<float>(offset.north)));
        }

        if (building.footprint.size() >= 3) {
            buildings.append(building);
        }
    }

    qDebug() << "[BuildingProvider] OSM建物変換:" << buildings.size() << "件";
    return buildings;
}

QVector<GroundPathData> BuildingProvider::parseOverpassPaths(const QByteArray &data,
                                                             double originLat,
                                                             double originLon) const
{
    QVector<GroundPathData> paths;

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "[BuildingProvider] OverpassパスJSON解析失敗:" << parseError.errorString();
        return paths;
    }

    QHash<qint64, Geo::GeoPoint> nodes;
    QVector<QJsonObject> ways;

    const QJsonArray elements = doc.object().value("elements").toArray();
    for (const QJsonValue &value : elements) {
        const QJsonObject obj = value.toObject();
        const QString type = obj.value("type").toString();
        if (type == "node") {
            nodes.insert(static_cast<qint64>(obj.value("id").toDouble()),
                         {obj.value("lat").toDouble(), obj.value("lon").toDouble()});
        } else if (type == "way") {
            const QJsonObject tags = obj.value("tags").toObject();
            if (tags.contains("highway") || tags.contains("railway")) {
                ways.append(obj);
            }
        }
    }

    for (const QJsonObject &way : ways) {
        const QJsonObject tags = way.value("tags").toObject();
        const QJsonArray nodeIds = way.value("nodes").toArray();
        if (nodeIds.size() < 2) {
            continue;
        }

        GroundPathData path;
        path.id = QString("osm_path_%1").arg(static_cast<qint64>(way.value("id").toDouble()));
        path.type = tags.contains("railway") ? "railway" : tags.value("highway").toString();
        path.width = pathWidthFromTags(tags);
        path.color = pathColorFromTags(tags);

        for (const QJsonValue &nodeValue : nodeIds) {
            const qint64 nodeId = static_cast<qint64>(nodeValue.toDouble());
            if (!nodes.contains(nodeId)) {
                path.points.clear();
                break;
            }
            const auto geo = nodes.value(nodeId);
            const auto offset = Geo::geoToRelative(originLat, originLon,
                                                   geo.latitude, geo.longitude);
            path.points.append(QVector2D(static_cast<float>(offset.east),
                                         static_cast<float>(offset.north)));
        }

        if (path.points.size() >= 2) {
            paths.append(path);
        }
    }

    qDebug() << "[BuildingProvider] OSM地表パス変換:" << paths.size() << "件";
    return paths;
}

QByteArray BuildingProvider::toCacheJson(const QVector<BuildingData> &buildings,
                                         const QVector<GroundPathData> &paths,
                                         double originLat,
                                         double originLon,
                                         int radiusMeters) const
{
    QJsonObject root;
    root["source"] = "overpass";
    root["radius_m"] = radiusMeters;

    QJsonObject origin;
    origin["latitude"] = originLat;
    origin["longitude"] = originLon;
    root["origin"] = origin;

    QJsonArray buildingArray;
    for (const BuildingData &building : buildings) {
        QJsonObject obj;
        obj["id"] = building.id;
        obj["name"] = building.name;
        obj["height"] = building.height;
        obj["color"] = building.color.name();

        QJsonArray footprint;
        for (const QVector2D &point : building.footprint) {
            QJsonObject pointObj;
            pointObj["east"] = point.x();
            pointObj["north"] = point.y();
            footprint.append(pointObj);
        }
        obj["footprint"] = footprint;
        buildingArray.append(obj);
    }
    root["buildings"] = buildingArray;

    QJsonArray pathArray;
    for (const GroundPathData &path : paths) {
        QJsonObject obj;
        obj["id"] = path.id;
        obj["type"] = path.type;
        obj["width"] = path.width;
        obj["color"] = path.color.name();

        QJsonArray points;
        for (const QVector2D &point : path.points) {
            QJsonObject pointObj;
            pointObj["east"] = point.x();
            pointObj["north"] = point.y();
            points.append(pointObj);
        }
        obj["points"] = points;
        pathArray.append(obj);
    }
    root["paths"] = pathArray;

    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

QVector<BuildingData> BuildingProvider::fallbackBuildings() const
{
    return BuildingLoader::loadFromJson(":/buildings/nerima_sample.json");
}

QVector<GroundPathData> BuildingProvider::fallbackPaths() const
{
    return BuildingLoader::loadPathsFromJson(":/buildings/nerima_sample.json");
}
