#ifndef FLIGHTCONTROLLER_H
#define FLIGHTCONTROLLER_H

#include <QObject>
#include "DroneState.h"

class DroneSimulator;

/**
 * @brief フライトコントローラ
 *
 * MAVLinkコマンドをシミュレーターのアクションに変換する。
 * フライトモード管理とコマンド応答処理を担当。
 */
class FlightController : public QObject
{
    Q_OBJECT

public:
    explicit FlightController(DroneSimulator *simulator, QObject *parent = nullptr);

    /// MAVLink コマンド処理 (MAV_CMD)
    /// @return true = ACCEPTED, false = DENIED
    bool handleCommand(uint16_t command, float param1, float param2,
                       float param3, float param4, float param5,
                       float param6, float param7);

    /// フライトモード設定 (MAVLink custom_mode)
    bool setFlightMode(uint32_t custom_mode);

    /// Arm/Disarm
    bool setArmed(bool armed);

signals:
    void commandCompleted(uint16_t command, uint8_t result);
    void modeChanged(FlightMode mode);

private:
    DroneSimulator *m_sim;
};

#endif // FLIGHTCONTROLLER_H
