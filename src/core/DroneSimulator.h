#ifndef DRONESIMULATOR_H
#define DRONESIMULATOR_H

#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include "DroneState.h"

/**
 * @brief 位置ベースのドローンシミュレーター
 *
 * 指令に応じて座標を直接移動する簡易シミュレーション。
 * 10ms周期でシミュレーションループを実行。
 */
class DroneSimulator : public QObject
{
    Q_OBJECT

public:
    explicit DroneSimulator(QObject *parent = nullptr);
    ~DroneSimulator() override;

    /// シミュレーション開始
    void start();
    /// シミュレーション停止
    void stop();

    /// 現在の状態を取得
    const DroneState& state() const { return m_state; }
    DroneState& state() { return m_state; }

    // === コマンド ===
    bool arm();
    bool disarm();
    bool takeoff(double altitude);
    bool land();
    bool returnToLaunch();
    void setMode(FlightMode mode);
    void setTargetPosition(double lat, double lon, double alt);

    /// 手動操作入力 (-1.0 ～ 1.0)
    void setManualInput(double roll, double pitch, double yaw, double throttle);

signals:
    /// 状態更新時に発火 (10ms間隔)
    void stateUpdated(const DroneState &state);
    /// ウェイポイント到着時に発火 (AUTOモード)
    void waypointReached();

private slots:
    void simulationStep();

private:
    void updateTakeoff(double dt);
    void updateLand(double dt);
    void updateGuided(double dt);
    void updateManual(double dt);
    void updateBattery(double dt);
    void updateVelocity(double dt);

    /// 2点間の距離計算 (メートル)
    double distanceBetween(double lat1, double lon1, double lat2, double lon2) const;
    /// 2点間の方位角計算 (ラジアン)
    double bearingBetween(double lat1, double lon1, double lat2, double lon2) const;

    DroneState m_state;
    QTimer m_simTimer;
    QElapsedTimer m_elapsedTimer;

    // ホームポジション
    double m_homeLat = 35.6812;
    double m_homeLon = 139.7671;
    double m_homeAlt = 0.0;

    // 手動入力
    double m_inputRoll     = 0.0;
    double m_inputPitch    = 0.0;
    double m_inputYaw      = 0.0;
    double m_inputThrottle = 0.0;

    // シミュレーションパラメータ
    static constexpr int SIM_INTERVAL_MS = 10;      // 10ms = 100Hz
    static constexpr double MAX_SPEED = 5.0;         // m/s 最大水平速度
    static constexpr double MAX_CLIMB_RATE = 2.0;    // m/s 最大上昇速度
    static constexpr double MAX_DESCENT_RATE = 1.5;  // m/s 最大降下速度
    static constexpr double YAW_RATE = 45.0;         // deg/s ヨーレート
    static constexpr double MOVE_SMOOTHING = 0.05;   // 移動スムージング係数

    // 内部状態
    bool m_isTakingOff = false;
    bool m_isLanding   = false;
};

#endif // DRONESIMULATOR_H
