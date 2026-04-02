#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLabel>

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

private:
    void setupUi();
    void setupConnections();
    void setupStyle();

    // Core
    DroneSimulator  *m_simulator;
    FlightController *m_flightController;

    // MAVLink
    MavlinkUdpLink  *m_udpLink;
    MavlinkManager  *m_mavManager;
    MessageHandler  *m_msgHandler;

    // GUI
    AttitudeIndicator *m_attitudeIndicator;
    TelemetryPanel    *m_telemetryPanel;
    MapView           *m_mapView;
    MapView3D         *m_mapView3D;
    ControlPanel      *m_controlPanel;

    // ステータスバー
    QLabel *m_lblStatus;
    QLabel *m_lblConnection;
};

#endif // MAINWINDOW_H
