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
        // GCSからのHeartbeat — 接続確認
        emit gcsHeartbeatReceived();
        emit logMessage(QString("← HEARTBEAT (sys:%1 comp:%2)")
                        .arg(msg.sysid).arg(msg.compid));
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
        // 未対応メッセージは無視（ただしログは出す）
        break;
    }
}

void MessageHandler::handleCommandLong(const mavlink_message_t &msg)
{
    mavlink_command_long_t cmd;
    mavlink_msg_command_long_decode(&msg, &cmd);

    emit logMessage(QString("← COMMAND_LONG cmd:%1 p1:%2 p7:%3")
                    .arg(cmd.command).arg(cmd.param1).arg(cmd.param7));

    // MAV_CMD_REQUEST_AUTOPILOT_CAPABILITIES (520)
    if (cmd.command == 520) {
        m_manager->sendAutopilotVersion();
        m_manager->sendCommandAck(cmd.command, MAV_RESULT_ACCEPTED,
                                  msg.sysid, msg.compid);
        emit logMessage("→ AUTOPILOT_VERSION 送信");
        return;
    }

    // MAV_CMD_REQUEST_MESSAGE (512) — メッセージ要求
    if (cmd.command == 512) {
        uint32_t msgId = static_cast<uint32_t>(cmd.param1);
        if (msgId == MAVLINK_MSG_ID_AUTOPILOT_VERSION) {
            m_manager->sendAutopilotVersion();
            m_manager->sendCommandAck(cmd.command, MAV_RESULT_ACCEPTED,
                                      msg.sysid, msg.compid);
            emit logMessage("→ AUTOPILOT_VERSION 送信 (REQUEST_MESSAGE)");
            return;
        }
        // その他のメッセージ要求は未対応
        m_manager->sendCommandAck(cmd.command, MAV_RESULT_UNSUPPORTED,
                                  msg.sysid, msg.compid);
        return;
    }

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

    emit logMessage(QString("→ COMMAND_ACK cmd:%1 result:%2")
                    .arg(cmd.command).arg(result ? "ACCEPTED" : "DENIED"));
}

void MessageHandler::handleCommandInt(const mavlink_message_t &msg)
{
    mavlink_command_int_t cmd;
    mavlink_msg_command_int_decode(&msg, &cmd);

    emit logMessage(QString("← COMMAND_INT cmd:%1").arg(cmd.command));

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

    emit logMessage(QString("← SET_MODE custom_mode:%1").arg(mode.custom_mode));

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

    // 実際にUDP経由で送信
    m_manager->sendRawMessage(reply);
    emit logMessage("← MISSION_REQUEST_LIST → ミッション数: 0 応答済み");
}

void MessageHandler::handleParamRequestList(const mavlink_message_t &msg)
{
    // パラメータ未実装 — PARAM_VALUE でパラメータ総数0を応答
    // QGCはこの応答がないとパラメータ読み込みで待機し続ける
    mavlink_message_t reply;

    // ダミーのパラメータ値をひとつ送信（param_count=1, param_index=0）
    // QGCは最低1つのPARAM_VALUEがないとエラーになるため
    mavlink_msg_param_value_pack(
        m_manager->systemId(), m_manager->componentId(), &reply,
        "SIM_ENABLED",          // param_id (16文字まで)
        1.0f,                   // param_value
        MAV_PARAM_TYPE_REAL32,  // param_type
        1,                      // param_count (総パラメータ数)
        0                       // param_index
    );

    m_manager->sendRawMessage(reply);
    emit logMessage("← PARAM_REQUEST_LIST → PARAM_VALUE (SIM_ENABLED=1) 応答済み");
}

void MessageHandler::handleRequestDataStream(const mavlink_message_t &msg)
{
    mavlink_request_data_stream_t req;
    mavlink_msg_request_data_stream_decode(&msg, &req);

    emit logMessage(QString("← REQUEST_DATA_STREAM id:%1 rate:%2")
                    .arg(req.req_stream_id).arg(req.req_message_rate));
    // テレメトリは常に送信中のため、特別な対応不要
}
