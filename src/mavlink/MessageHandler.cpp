#include "MessageHandler.h"
#include "MavlinkManager.h"
#include "core/FlightController.h"
#include "core/MissionManager.h"
#include <QDebug>

MessageHandler::MessageHandler(MavlinkManager *manager,
                               FlightController *controller,
                               MissionManager *missionMgr,
                               QObject *parent)
    : QObject(parent)
    , m_manager(manager)
    , m_controller(controller)
    , m_missionMgr(missionMgr)
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

    case MAVLINK_MSG_ID_MISSION_COUNT:
        handleMissionCount(msg);
        break;

    case MAVLINK_MSG_ID_MISSION_ITEM_INT:
        handleMissionItemInt(msg);
        break;

    case MAVLINK_MSG_ID_MISSION_REQUEST_INT:
        handleMissionRequestInt(msg);
        break;

    case MAVLINK_MSG_ID_MISSION_SET_CURRENT:
        handleMissionSetCurrent(msg);
        break;

    case MAVLINK_MSG_ID_MISSION_CLEAR_ALL:
        handleMissionClearAll(msg);
        break;

    case MAVLINK_MSG_ID_MISSION_ACK:
        handleMissionAck(msg);
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

    // MAV_CMD_MISSION_START (300)
    if (cmd.command == 300) {
        m_missionMgr->startMission();
        m_manager->sendCommandAck(cmd.command, MAV_RESULT_ACCEPTED,
                                  msg.sysid, msg.compid);
        emit logMessage("→ MISSION_START 実行");
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

// ============================================================
// ミッションプロトコル
// ============================================================

void MessageHandler::handleMissionRequestList(const mavlink_message_t &msg)
{
    // ミッションアイテム数を返す
    uint16_t count = static_cast<uint16_t>(m_missionMgr->itemCount());
    m_manager->sendMissionCount(count, msg.sysid, msg.compid);
    emit logMessage(QString("← MISSION_REQUEST_LIST → ミッション数: %1 応答済み").arg(count));
}

void MessageHandler::handleMissionCount(const mavlink_message_t &msg)
{
    // GCSからミッションアップロード開始
    mavlink_mission_count_t mc;
    mavlink_msg_mission_count_decode(&msg, &mc);

    m_uploadCount = mc.count;
    m_uploadReceived = 0;
    m_uploadTargetSys = msg.sysid;
    m_uploadTargetComp = msg.compid;
    m_uploadItems.clear();
    m_uploadItems.resize(mc.count);

    emit logMessage(QString("← MISSION_COUNT count:%1 アップロード開始").arg(mc.count));

    if (mc.count > 0) {
        // 最初のアイテムを要求
        m_manager->sendMissionRequestInt(0, msg.sysid, msg.compid);
    } else {
        // 空ミッション
        m_missionMgr->clearAll();
        m_manager->sendMissionAck(MAV_MISSION_ACCEPTED, msg.sysid, msg.compid);
    }
}

void MessageHandler::handleMissionItemInt(const mavlink_message_t &msg)
{
    mavlink_mission_item_int_t mi;
    mavlink_msg_mission_item_int_decode(&msg, &mi);

    if (mi.seq >= m_uploadCount) {
        m_manager->sendMissionAck(MAV_MISSION_INVALID_SEQUENCE, msg.sysid, msg.compid);
        return;
    }

    MissionItem item;
    item.seq = mi.seq;
    item.command = mi.command;
    item.frame = mi.frame;
    item.current = mi.current;
    item.autocontinue = mi.autocontinue;
    item.param1 = mi.param1;
    item.param2 = mi.param2;
    item.param3 = mi.param3;
    item.param4 = mi.param4;
    item.latitude = mi.x / 1e7;
    item.longitude = mi.y / 1e7;
    item.altitude = static_cast<double>(mi.z);

    m_uploadItems[mi.seq] = item;
    m_uploadReceived++;

    emit logMessage(QString("← MISSION_ITEM_INT seq:%1 cmd:%2 lat:%3 lon:%4")
                    .arg(mi.seq).arg(mi.command)
                    .arg(item.latitude, 0, 'f', 5).arg(item.longitude, 0, 'f', 5));

    if (m_uploadReceived < m_uploadCount) {
        // 次のアイテムを要求
        m_manager->sendMissionRequestInt(m_uploadReceived, msg.sysid, msg.compid);
    } else {
        // アップロード完了
        m_missionMgr->setItems(m_uploadItems);
        m_manager->sendMissionAck(MAV_MISSION_ACCEPTED, msg.sysid, msg.compid);
        emit logMessage(QString("→ MISSION_ACK アップロード完了 (%1 items)").arg(m_uploadCount));
        m_uploadItems.clear();
    }
}

void MessageHandler::handleMissionRequestInt(const mavlink_message_t &msg)
{
    mavlink_mission_request_int_t req;
    mavlink_msg_mission_request_int_decode(&msg, &req);

    if (req.seq < m_missionMgr->itemCount()) {
        const auto &item = m_missionMgr->items().at(req.seq);
        m_manager->sendMissionItemInt(item, msg.sysid, msg.compid);
        emit logMessage(QString("← MISSION_REQUEST_INT seq:%1 → 送信").arg(req.seq));
    } else {
        emit logMessage(QString("← MISSION_REQUEST_INT seq:%1 範囲外").arg(req.seq));
    }
}

void MessageHandler::handleMissionSetCurrent(const mavlink_message_t &msg)
{
    mavlink_mission_set_current_t sc;
    mavlink_msg_mission_set_current_decode(&msg, &sc);

    m_missionMgr->setCurrentWaypoint(sc.seq);
    m_manager->sendMissionCurrent(sc.seq);
    emit logMessage(QString("← MISSION_SET_CURRENT seq:%1").arg(sc.seq));
}

void MessageHandler::handleMissionClearAll(const mavlink_message_t &msg)
{
    m_missionMgr->clearAll();
    m_manager->sendMissionAck(MAV_MISSION_ACCEPTED, msg.sysid, msg.compid);
    emit logMessage("← MISSION_CLEAR_ALL → ミッションクリア");
}

void MessageHandler::handleMissionAck(const mavlink_message_t &msg)
{
    mavlink_mission_ack_t ack;
    mavlink_msg_mission_ack_decode(&msg, &ack);
    emit logMessage(QString("← MISSION_ACK type:%1").arg(ack.type));
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
