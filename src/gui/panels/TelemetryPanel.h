#ifndef TELEMETRYPANEL_H
#define TELEMETRYPANEL_H

#include <QWidget>
#include <QLabel>
#include <QProgressBar>
#include <QGridLayout>
#include "core/DroneState.h"

/**
 * @brief テレメトリダッシュボードパネル
 *
 * 速度、高度、GPS、バッテリー等のリアルタイム表示。
 */
class TelemetryPanel : public QWidget
{
    Q_OBJECT

public:
    explicit TelemetryPanel(QWidget *parent = nullptr);

    void updateTelemetry(const DroneState &state);

private:
    QLabel* createValueLabel(const QString &initialText = "--");
    QLabel* createTitleLabel(const QString &text);
    void setupUi();

    QLabel *m_lblAltitude;
    QLabel *m_lblAltitudeMsl;
    QLabel *m_lblGroundSpeed;
    QLabel *m_lblAirSpeed;
    QLabel *m_lblClimbRate;
    QLabel *m_lblLatitude;
    QLabel *m_lblLongitude;
    QLabel *m_lblGpsFix;
    QLabel *m_lblSatellites;
    QLabel *m_lblHeading;
    QLabel *m_lblFlightMode;
    QLabel *m_lblArmedState;
    QLabel *m_lblBatteryVoltage;
    QProgressBar *m_batteryBar;
    QLabel *m_lblFlightTime;
};

#endif // TELEMETRYPANEL_H
