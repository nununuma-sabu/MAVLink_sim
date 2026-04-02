#include "FlightController.h"
#include "DroneSimulator.h"
#include <QDebug>

// MAV_CMD 定義
static constexpr uint16_t MAV_CMD_COMPONENT_ARM_DISARM = 400;
static constexpr uint16_t MAV_CMD_NAV_TAKEOFF          = 22;
static constexpr uint16_t MAV_CMD_NAV_LAND             = 21;
static constexpr uint16_t MAV_CMD_NAV_RETURN_TO_LAUNCH = 20;
static constexpr uint16_t MAV_CMD_NAV_WAYPOINT         = 16;
static constexpr uint16_t MAV_CMD_DO_SET_MODE           = 176;

// MAV_RESULT
static constexpr uint8_t MAV_RESULT_ACCEPTED = 0;
static constexpr uint8_t MAV_RESULT_DENIED   = 1;
static constexpr uint8_t MAV_RESULT_UNSUPPORTED = 3;

FlightController::FlightController(DroneSimulator *simulator, QObject *parent)
    : QObject(parent)
    , m_sim(simulator)
{
}

bool FlightController::handleCommand(uint16_t command,
                                      float param1, float param2,
                                      float param3, float param4,
                                      float param5, float param6, float param7)
{
    uint8_t result = MAV_RESULT_UNSUPPORTED;

    switch (command) {
    case MAV_CMD_COMPONENT_ARM_DISARM: {
        // param1: 1=arm, 0=disarm
        bool doArm = (param1 > 0.5f);
        bool ok = doArm ? m_sim->arm() : m_sim->disarm();
        result = ok ? MAV_RESULT_ACCEPTED : MAV_RESULT_DENIED;
        break;
    }
    case MAV_CMD_NAV_TAKEOFF: {
        // param7: 目標高度
        double alt = (param7 > 0) ? static_cast<double>(param7) : 10.0;
        bool ok = m_sim->takeoff(alt);
        result = ok ? MAV_RESULT_ACCEPTED : MAV_RESULT_DENIED;
        break;
    }
    case MAV_CMD_NAV_LAND: {
        bool ok = m_sim->land();
        result = ok ? MAV_RESULT_ACCEPTED : MAV_RESULT_DENIED;
        break;
    }
    case MAV_CMD_NAV_RETURN_TO_LAUNCH: {
        bool ok = m_sim->returnToLaunch();
        result = ok ? MAV_RESULT_ACCEPTED : MAV_RESULT_DENIED;
        break;
    }
    case MAV_CMD_NAV_WAYPOINT: {
        // param5=lat, param6=lon, param7=alt
        m_sim->setTargetPosition(static_cast<double>(param5),
                                 static_cast<double>(param6),
                                 static_cast<double>(param7));
        result = MAV_RESULT_ACCEPTED;
        break;
    }
    case MAV_CMD_DO_SET_MODE: {
        uint32_t mode = static_cast<uint32_t>(param1);
        bool ok = setFlightMode(mode);
        result = ok ? MAV_RESULT_ACCEPTED : MAV_RESULT_DENIED;
        break;
    }
    default:
        qDebug() << "[FlightController] 未対応コマンド:" << command;
        result = MAV_RESULT_UNSUPPORTED;
        break;
    }

    emit commandCompleted(command, result);
    return (result == MAV_RESULT_ACCEPTED);
}

bool FlightController::setFlightMode(uint32_t custom_mode)
{
    FlightMode mode;
    switch (custom_mode) {
    case 0: mode = FlightMode::STABILIZE; break;
    case 3: mode = FlightMode::AUTO;      break;
    case 4: mode = FlightMode::GUIDED;    break;
    case 5: mode = FlightMode::LOITER;    break;
    case 6: mode = FlightMode::RTL;       break;
    case 9: mode = FlightMode::LAND;      break;
    default:
        qDebug() << "[FlightController] 未知のモード:" << custom_mode;
        return false;
    }

    m_sim->setMode(mode);
    emit modeChanged(mode);
    return true;
}

bool FlightController::setArmed(bool armed)
{
    return armed ? m_sim->arm() : m_sim->disarm();
}
