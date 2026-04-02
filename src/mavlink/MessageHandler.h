#ifndef MESSAGEHANDLER_H
#define MESSAGEHANDLER_H

#include <QObject>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
#include <common/mavlink.h>
#pragma GCC diagnostic pop

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
                            QObject *parent = nullptr);

public slots:
    /// MAVLinkメッセージの処理
    void handleMessage(const mavlink_message_t &msg);

private:
    void handleCommandLong(const mavlink_message_t &msg);
    void handleCommandInt(const mavlink_message_t &msg);
    void handleSetMode(const mavlink_message_t &msg);
    void handleMissionRequestList(const mavlink_message_t &msg);
    void handleParamRequestList(const mavlink_message_t &msg);
    void handleRequestDataStream(const mavlink_message_t &msg);

    MavlinkManager *m_manager;
    FlightController *m_controller;
};

#endif // MESSAGEHANDLER_H
