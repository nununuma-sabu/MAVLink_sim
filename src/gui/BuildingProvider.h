#ifndef BUILDINGPROVIDER_H
#define BUILDINGPROVIDER_H

#include "BuildingLoader.h"
#include <QNetworkAccessManager>
#include <QObject>

class BuildingProvider : public QObject
{
    Q_OBJECT

public:
    explicit BuildingProvider(QObject *parent = nullptr);

    void loadForOrigin(double latitude, double longitude, int radiusMeters = 300);

signals:
    void buildingsReady(const QVector<BuildingData> &buildings, const QString &source);
    void statusMessage(const QString &message);

private:
    QString cachePath(double latitude, double longitude, int radiusMeters) const;
    QString buildOverpassQuery(double latitude, double longitude, int radiusMeters) const;
    QVector<BuildingData> parseOverpassJson(const QByteArray &data,
                                            double originLat,
                                            double originLon) const;
    QByteArray toCacheJson(const QVector<BuildingData> &buildings,
                           double originLat,
                           double originLon,
                           int radiusMeters) const;
    QVector<BuildingData> fallbackBuildings() const;

    QNetworkAccessManager m_network;
};

#endif // BUILDINGPROVIDER_H
