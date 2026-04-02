#include "MessageHandler.h"
#include "MavlinkManager.h"
#include "core/FlightController.h"
#include <QDebug>

MessageHandler::MessageHandler(MavlinkManager *manager,
                               FlightController *controller,
                               QObject *parent)
    : QObject(parent)
    , m_manager(manager)
    , m_controller(controller)
{
}

void MessageHandler::handleMessage(const mavlink_message_t &msg)
{
    switch (msg.msgid) {
    case MAVLINK_MSG_ID_HEARTBEAT:
        // GCSからのHeartbeat — 接続確認用、特別な処理不要
        break;

    case MAVLINK_MSG_ID_COMMAND_LONG:
        handleCommandLong(msg);
        break;

    case MAVLINK_MSG_ID_COMMAND_INT:
        handleCommandInt(msg);
        break;

    case MAVLINK_MSG_ID_SET_MODE:
        handleSetMode(msg);
        break;

    case MAVLINK_MSG_ID_MISSION_REQUEST_LIST:
        handleMissionRequestList(msg);
        break;

    case MAVLINK_MSG_ID_PARAM_REQUEST_LIST:
        handleParamRequestList(msg);
        break;

    case MAVLINK_MSG_ID_REQUEST_DATA_STREAM:
        handleRequestDataStream(msg);
        break;

    default:
        // 未対応メッセージは無視
        break;
    }
}

void MessageHandler::handleCommandLong(const mavlink_message_t &msg)
{
    mavlink_command_long_t cmd;
    mavlink_msg_command_long_decode(&msg, &cmd);

    qDebug() << "[MessageHandler] COMMAND_LONG 受信 cmd:" << cmd.command
             << "param1:" << cmd.param1;

    bool result = m_controller->handleCommand(
        cmd.command,
        cmd.param1, cmd.param2, cmd.param3, cmd.param4,
        cmd.param5, cmd.param6, cmd.param7
    );

    m_manager->sendCommandAck(
        cmd.command,
        result ? MAV_RESULT_ACCEPTED : MAV_RESULT_DENIED,
        msg.sysid,
        msg.compid
    );
}

void MessageHandler::handleCommandInt(const mavlink_message_t &msg)
{
    mavlink_command_int_t cmd;
    mavlink_msg_command_int_decode(&msg, &cmd);

    qDebug() << "[MessageHandler] COMMAND_INT 受信 cmd:" << cmd.command;

    // COMMAND_INTではx,yがint32 (lat/lon * 1e7)
    float param5 = static_cast<float>(cmd.x / 1e7);
    float param6 = static_cast<float>(cmd.y / 1e7);

    bool result = m_controller->handleCommand(
        cmd.command,
        cmd.param1, cmd.param2, cmd.param3, cmd.param4,
        param5, param6, cmd.z
    );

    m_manager->sendCommandAck(
        cmd.command,
        result ? MAV_RESULT_ACCEPTED : MAV_RESULT_DENIED,
        msg.sysid,
        msg.compid
    );
}

void MessageHandler::handleSetMode(const mavlink_message_t &msg)
{
    mavlink_set_mode_t mode;
    mavlink_msg_set_mode_decode(&msg, &mode);

    qDebug() << "[MessageHandler] SET_MODE 受信 custom_mode:" << mode.custom_mode;

    m_controller->setFlightMode(mode.custom_mode);
}

void MessageHandler::handleMissionRequestList(const mavlink_message_t &msg)
{
    // ミッションカウント0を返す（ミッション未実装）
    mavlink_message_t reply;
    mavlink_msg_mission_count_pack(
        m_manager->systemId(), m_manager->componentId(), &reply,
        msg.sysid, msg.compid,
        0,                          // count
        MAV_MISSION_TYPE_MISSION,   // mission_type
        0                           // opaque_id
    );

    // MavlinkManagerからリンクへ送信するためにここでは直接送信できないため、
    // 将来的にはmanager経由で送信する仕組みが必要
    qDebug() << "[MessageHandler] MISSION_REQUEST_LIST — ミッション数: 0";
}

void MessageHandler::handleParamRequestList(const mavlink_message_t &msg)
{
    Q_UNUSED(msg);
    // パラメータ未実装 — パラメータカウント0で応答
    qDebug() << "[MessageHandler] PARAM_REQUEST_LIST — パラメータ未実装";
}

void MessageHandler::handleRequestDataStream(const mavlink_message_t &msg)
{
    mavlink_request_data_stream_t req;
    mavlink_msg_request_data_stream_decode(&msg, &req);

    qDebug() << "[MessageHandler] REQUEST_DATA_STREAM stream_id:" << req.req_stream_id
             << "rate:" << req.req_message_rate;
    // テレメトリは常に送信中のため、特別な対応不要
}
