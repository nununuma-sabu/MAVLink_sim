#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QTimer>
#include <QVector>
#include "core/AppSettings.h"
#include "core/MapLocation.h"

class DroneSimulator;
class FlightController;
class MavlinkUdpLink;
class MavlinkManager;
class MessageHandler;
class AttitudeIndicator;
class TelemetryPanel;
class MapView;
class MapView3D;
class ControlPanel;
class LogPanel;
class MissionPanel;
class MissionManager;

struct DroneState;

/**
 * @brief メインウィンドウ
 *
 * 全コンポーネントを統合するアプリケーションウィンドウ。
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onStateUpdated(const DroneState &state);
    void onArmRequested();
    void onDisarmRequested();
    void onTakeoffRequested(double altitude);
    void onLandRequested();
    void onRtlRequested();
    void onModeChanged(int modeIndex);
    void onMapLocationChangeRequested(int locationIndex);

private:
    void setupUi();
    void setupConnections();
    void setupStyle();
    bool startMavlinkConnection();
    void resetSimulationToLocation(const MapLocation &location);

    // Core
    DroneSimulator  *m_simulator;
    FlightController *m_flightController;

    // MAVLink
    MavlinkUdpLink  *m_udpLink;
    MavlinkManager  *m_mavManager;
    MessageHandler  *m_msgHandler;
    MavlinkConnectionSettings m_mavlinkSettings;

    // GUI
    AttitudeIndicator *m_attitudeIndicator;
    TelemetryPanel    *m_telemetryPanel;
    MapView           *m_mapView;
    MapView3D         *m_mapView3D;
    ControlPanel      *m_controlPanel;
    QVector<MapLocation> m_locations;
    int m_currentLocationIndex = 0;

    // Mission
    MissionManager    *m_missionManager;
    MissionPanel      *m_missionPanel;

    // ステータスバー
    QLabel *m_lblStatus;
    QLabel *m_lblConnection;
    QLabel *m_lblGcsStatus;

    // GCS Heartbeat タイムアウト
    QTimer m_gcsTimeoutTimer;

    // ログパネル
    LogPanel *m_logPanel;
};

#endif // MAINWINDOW_H
