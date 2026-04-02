#ifndef MAVLINKMANAGER_H
#define MAVLINKMANAGER_H

#include <QObject>
#include <QTimer>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
#include <common/mavlink.h>
#pragma GCC diagnostic pop

#include "core/DroneState.h"

class MavlinkUdpLink;

/**
 * @brief MAVLink メッセージ送受信管理
 *
 * DroneStateからMAVLinkメッセージを生成し定期送信。
 * 受信メッセージのデコードと振り分け。
 */
class MavlinkManager : public QObject
{
    Q_OBJECT

public:
    explicit MavlinkManager(MavlinkUdpLink *link, QObject *parent = nullptr);

    /// テレメトリ定期送信を開始
    void startTelemetry();
    /// テレメトリ送信を停止
    void stopTelemetry();

    /// ドローン状態を更新
    void updateState(const DroneState &state);

    /// システムID / コンポーネントID
    uint8_t systemId() const { return m_sysId; }
    uint8_t componentId() const { return m_compId; }

    // === 送信関数 ===
    void sendHeartbeat();
    void sendSysStatus();
    void sendGpsRawInt();
    void sendAttitude();
    void sendGlobalPositionInt();
    void sendBatteryStatus();
    void sendVfrHud();

    /// COMMAND_ACK を送信
    void sendCommandAck(uint16_t command, uint8_t result, uint8_t targetSys, uint8_t targetComp);

signals:
    /// コマンド受信
    void commandReceived(uint16_t command, float param1, float param2,
                         float param3, float param4, float param5,
                         float param6, float param7,
                         uint8_t targetSys, uint8_t targetComp);
    /// モード変更要求
    void setModeRequested(uint32_t customMode);

private slots:
    void onSlowTimer();   // 1Hz: Heartbeat, SysStatus, GPS, Battery
    void onFastTimer();   // 10Hz: Attitude, GlobalPosition, VfrHud

private:
    MavlinkUdpLink *m_link;
    DroneState m_state;

    QTimer m_slowTimer;  // 1Hz
    QTimer m_fastTimer;  // 10Hz

    uint8_t m_sysId  = 1;   // システムID
    uint8_t m_compId = 1;   // コンポーネントID (MAV_COMP_ID_AUTOPILOT1)
};

#endif // MAVLINKMANAGER_H
