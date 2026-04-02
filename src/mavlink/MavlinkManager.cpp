#include "MavlinkManager.h"
#include "MavlinkUdpLink.h"
#include <QDebug>
#include <QtMath>

MavlinkManager::MavlinkManager(MavlinkUdpLink *link, QObject *parent)
    : QObject(parent)
    , m_link(link)
{
    connect(&m_slowTimer, &QTimer::timeout, this, &MavlinkManager::onSlowTimer);
    connect(&m_fastTimer, &QTimer::timeout, this, &MavlinkManager::onFastTimer);
}

void MavlinkManager::startTelemetry()
{
    m_slowTimer.start(1000);  // 1Hz
    m_fastTimer.start(100);   // 10Hz
    qDebug() << "[MavlinkManager] テレメトリ送信開始";
}

void MavlinkManager::stopTelemetry()
{
    m_slowTimer.stop();
    m_fastTimer.stop();
    qDebug() << "[MavlinkManager] テレメトリ送信停止";
}

void MavlinkManager::updateState(const DroneState &state)
{
    m_state = state;
}

// ============================================================
// タイマーコールバック
// ============================================================

void MavlinkManager::onSlowTimer()
{
    sendHeartbeat();
    sendSysStatus();
    sendGpsRawInt();
    sendBatteryStatus();
}

void MavlinkManager::onFastTimer()
{
    sendAttitude();
    sendGlobalPositionInt();
    sendVfrHud();
}

// ============================================================
// 送信関数
// ============================================================

void MavlinkManager::sendHeartbeat()
{
    mavlink_message_t msg;

    uint8_t base_mode = MAV_MODE_FLAG_CUSTOM_MODE_ENABLED;
    if (m_state.armed) {
        base_mode |= MAV_MODE_FLAG_SAFETY_ARMED;
    }
    base_mode |= MAV_MODE_FLAG_GUIDED_ENABLED;

    mavlink_msg_heartbeat_pack(
        m_sysId, m_compId, &msg,
        MAV_TYPE_QUADROTOR,            // type
        MAV_AUTOPILOT_ARDUPILOTMEGA,   // autopilot
        base_mode,                      // base_mode
        static_cast<uint32_t>(m_state.flight_mode), // custom_mode
        m_state.system_status           // system_status
    );
    m_link->sendMessage(msg);
}

void MavlinkManager::sendSysStatus()
{
    mavlink_message_t msg;
    mavlink_msg_sys_status_pack(
        m_sysId, m_compId, &msg,
        0xFFFF,  // sensors present
        0xFFFF,  // sensors enabled
        0xFFFF,  // sensors health
        500,     // load (50%)
        static_cast<uint16_t>(m_state.battery_voltage * 1000),  // voltage mV
        static_cast<int16_t>(m_state.battery_current * 100),    // current cA
        static_cast<int8_t>(m_state.battery_remaining),         // remaining %
        0, 0, 0, 0, 0, 0,  // drop rates, errors
        0, 0, 0             // extended sensors (present, enabled, health)
    );
    m_link->sendMessage(msg);
}

void MavlinkManager::sendGpsRawInt()
{
    mavlink_message_t msg;
    mavlink_msg_gps_raw_int_pack(
        m_sysId, m_compId, &msg,
        m_state.boot_time_ms * 1000,  // time_usec
        m_state.gps_fix_type,
        static_cast<int32_t>(m_state.latitude * 1e7),
        static_cast<int32_t>(m_state.longitude * 1e7),
        static_cast<int32_t>(m_state.altitude_msl * 1000),  // alt mm
        UINT16_MAX,  // eph (unknown)
        UINT16_MAX,  // epv (unknown)
        static_cast<uint16_t>(m_state.groundspeed * 100),   // vel cm/s
        static_cast<uint16_t>(qRadiansToDegrees(m_state.yaw) * 100), // cog
        static_cast<uint8_t>(m_state.satellites_visible),
        0, 0, 0, 0, 0,  // alt_ellipsoid, h_acc, v_acc, vel_acc, hdg_acc
        0                // yaw (cdeg, 0=unknown)
    );
    m_link->sendMessage(msg);
}

void MavlinkManager::sendAttitude()
{
    mavlink_message_t msg;
    mavlink_msg_attitude_pack(
        m_sysId, m_compId, &msg,
        static_cast<uint32_t>(m_state.boot_time_ms),
        static_cast<float>(m_state.roll),
        static_cast<float>(m_state.pitch),
        static_cast<float>(m_state.yaw),
        0.0f,  // rollspeed
        0.0f,  // pitchspeed
        0.0f   // yawspeed
    );
    m_link->sendMessage(msg);
}

void MavlinkManager::sendGlobalPositionInt()
{
    mavlink_message_t msg;
    mavlink_msg_global_position_int_pack(
        m_sysId, m_compId, &msg,
        static_cast<uint32_t>(m_state.boot_time_ms),
        static_cast<int32_t>(m_state.latitude * 1e7),
        static_cast<int32_t>(m_state.longitude * 1e7),
        static_cast<int32_t>(m_state.altitude_msl * 1000),  // mm
        static_cast<int32_t>(m_state.altitude * 1000),       // mm relative
        static_cast<int16_t>(m_state.vx * 100),  // cm/s
        static_cast<int16_t>(m_state.vy * 100),
        static_cast<int16_t>(m_state.vz * 100),
        static_cast<uint16_t>(qRadiansToDegrees(m_state.yaw) * 100)  // cdeg
    );
    m_link->sendMessage(msg);
}

void MavlinkManager::sendBatteryStatus()
{
    mavlink_message_t msg;
    uint16_t voltages[10] = {};
    voltages[0] = static_cast<uint16_t>(m_state.battery_voltage * 1000);
    for (int i = 1; i < 10; i++) voltages[i] = UINT16_MAX;

    uint16_t voltages_ext[4] = {0, 0, 0, 0};

    mavlink_msg_battery_status_pack(
        m_sysId, m_compId, &msg,
        0,                          // battery id
        MAV_BATTERY_FUNCTION_ALL,
        MAV_BATTERY_TYPE_LIPO,
        INT16_MAX,                  // temperature (unknown)
        voltages,
        static_cast<int16_t>(m_state.battery_current * 100),
        -1,                         // current consumed (mAh, unknown)
        -1,                         // energy consumed (hJ, unknown)
        static_cast<int8_t>(m_state.battery_remaining),
        0,                          // time remaining
        MAV_BATTERY_CHARGE_STATE_OK,
        voltages_ext,
        0,                          // mode
        0                           // fault_bitmask
    );
    m_link->sendMessage(msg);
}

void MavlinkManager::sendVfrHud()
{
    mavlink_message_t msg;
    mavlink_msg_vfr_hud_pack(
        m_sysId, m_compId, &msg,
        static_cast<float>(m_state.airspeed),
        static_cast<float>(m_state.groundspeed),
        static_cast<int16_t>(qRadiansToDegrees(m_state.yaw)),
        50,  // throttle %
        static_cast<float>(m_state.altitude_msl),
        static_cast<float>(m_state.climb_rate)
    );
    m_link->sendMessage(msg);
}

void MavlinkManager::sendCommandAck(uint16_t command, uint8_t result,
                                     uint8_t targetSys, uint8_t targetComp)
{
    mavlink_message_t msg;
    mavlink_msg_command_ack_pack(
        m_sysId, m_compId, &msg,
        command,
        result,
        0,          // progress
        0,          // result_param2
        targetSys,
        targetComp
    );
    m_link->sendMessage(msg);
}
