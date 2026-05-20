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

} // namespace

QVector<BuildingData> BuildingLoader::loadFromJson(const QString &path)
{
    QVector<BuildingData> buildings;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "[BuildingLoader] 建物JSONを開けません:" << path << file.errorString();
        return buildings;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "[BuildingLoader] JSON解析失敗:" << path << parseError.errorString();
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

        const QString colorName = obj.value("color").toString();
        if (!colorName.isEmpty()) {
            const QColor parsed(colorName);
            if (parsed.isValid()) {
                building.color = parsed;
            }
        }

        for (const QJsonValue &pointValue : footprintArray) {
            building.footprint.append(parsePoint(pointValue.toObject()));
        }

        if (building.height <= 0.0f) {
            building.height = 8.0f;
        }
        buildings.append(building);
    }

    qDebug() << "[BuildingLoader] 建物読み込み:" << buildings.size() << "件";
    return buildings;
}
