#include "BuildingLoader.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QDebug>

namespace {

QVector2D parsePoint(const QJsonObject &obj)
{
    return QVector2D(static_cast<float>(obj.value("east").toDouble()),
                     static_cast<float>(obj.value("north").toDouble()));
}

QColor parseColor(const QJsonObject &obj, const QString &key, const QColor &fallback)
{
    const QString colorName = obj.value(key).toString();
    if (colorName.isEmpty()) {
        return fallback;
    }
    const QColor parsed(colorName);
    return parsed.isValid() ? parsed : fallback;
}

} // namespace

QVector<BuildingData> BuildingLoader::loadFromJson(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "[BuildingLoader] 建物JSONを開けません:" << path << file.errorString();
        return {};
    }

    return loadFromJsonData(file.readAll(), path);
}

QVector<BuildingData> BuildingLoader::loadFromJsonData(const QByteArray &data,
                                                       const QString &sourceName)
{
    QVector<BuildingData> buildings;

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "[BuildingLoader] JSON解析失敗:" << sourceName << parseError.errorString();
        return buildings;
    }

    const QJsonArray array = doc.object().value("buildings").toArray();
    for (const QJsonValue &value : array) {
        const QJsonObject obj = value.toObject();
        const QJsonArray footprintArray = obj.value("footprint").toArray();
        if (footprintArray.size() < 3) {
            continue;
        }

        BuildingData building;
        building.id = obj.value("id").toString();
        building.name = obj.value("name").toString();
        building.height = static_cast<float>(obj.value("height").toDouble(8.0));

        building.color = parseColor(obj, "color", building.color);

        for (const QJsonValue &pointValue : footprintArray) {
            building.footprint.append(parsePoint(pointValue.toObject()));
        }

        if (building.height <= 0.0f) {
            building.height = 8.0f;
        }
        buildings.append(building);
    }

    qDebug() << "[BuildingLoader] 建物読み込み:" << buildings.size() << "件" << sourceName;
    return buildings;
}

QVector<GroundPathData> BuildingLoader::loadPathsFromJson(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "[BuildingLoader] 地表パスJSONを開けません:" << path << file.errorString();
        return {};
    }

    return loadPathsFromJsonData(file.readAll(), path);
}

QVector<GroundPathData> BuildingLoader::loadPathsFromJsonData(const QByteArray &data,
                                                              const QString &sourceName)
{
    QVector<GroundPathData> paths;

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "[BuildingLoader] 地表パスJSON解析失敗:" << sourceName << parseError.errorString();
        return paths;
    }

    const QJsonArray array = doc.object().value("paths").toArray();
    for (const QJsonValue &value : array) {
        const QJsonObject obj = value.toObject();
        const QJsonArray pointsArray = obj.value("points").toArray();
        if (pointsArray.size() < 2) {
            continue;
        }

        GroundPathData path;
        path.id = obj.value("id").toString();
        path.type = obj.value("type").toString();
        path.width = static_cast<float>(obj.value("width").toDouble(path.width));
        path.color = parseColor(obj, "color", path.color);

        for (const QJsonValue &pointValue : pointsArray) {
            path.points.append(parsePoint(pointValue.toObject()));
        }

        if (path.width <= 0.0f) {
            path.width = 4.0f;
        }
        paths.append(path);
    }

    qDebug() << "[BuildingLoader] 地表パス読み込み:" << paths.size() << "件" << sourceName;
    return paths;
}
