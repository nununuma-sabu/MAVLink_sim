#ifndef MESSAGEHANDLER_H
#define MESSAGEHANDLER_H

#include <QObject>
#include <QVector>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
#include <common/mavlink.h>
#pragma GCC diagnostic pop

#include "core/MissionManager.h"

class MavlinkManager;
class FlightController;

/**
 * @brief MAVLink受信メッセージのハンドラ
 *
 * 受信したMAVLinkメッセージをデコードし、
 * FlightControllerやMavlinkManagerへ適切に振り分ける。
 */
class MessageHandler : public QObject
{
    Q_OBJECT

public:
    explicit MessageHandler(MavlinkManager *manager,
                            FlightController *controller,
                            MissionManager *missionMgr,
                            QObject *parent = nullptr);

public slots:
    /// MAVLinkメッセージの処理
    void handleMessage(const mavlink_message_t &msg);

signals:
    /// GCSからHeartbeat受信
    void gcsHeartbeatReceived();
    /// ログメッセージ
    void logMessage(const QString &msg);

private:
    void handleCommandLong(const mavlink_message_t &msg);
    void handleCommandInt(const mavlink_message_t &msg);
    void handleSetMode(const mavlink_message_t &msg);
    void handleMissionRequestList(const mavlink_message_t &msg);
    void handleMissionCount(const mavlink_message_t &msg);
    void handleMissionItemInt(const mavlink_message_t &msg);
    void handleMissionRequestInt(const mavlink_message_t &msg);
    void handleMissionSetCurrent(const mavlink_message_t &msg);
    void handleMissionClearAll(const mavlink_message_t &msg);
    void handleMissionAck(const mavlink_message_t &msg);
    void handleParamRequestList(const mavlink_message_t &msg);
    void handleRequestDataStream(const mavlink_message_t &msg);

    MavlinkManager *m_manager;
    FlightController *m_controller;
    MissionManager *m_missionMgr;

    // ミッションアップロード用
    uint16_t m_uploadCount = 0;
    uint16_t m_uploadReceived = 0;
    uint8_t m_uploadTargetSys = 0;
    uint8_t m_uploadTargetComp = 0;
    QVector<MissionItem> m_uploadItems;
};

#endif // MESSAGEHANDLER_H
