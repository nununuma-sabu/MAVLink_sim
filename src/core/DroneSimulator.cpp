#include "DroneSimulator.h"
#include <QtMath>
#include <QDebug>

DroneSimulator::DroneSimulator(QObject *parent)
    : QObject(parent)
{
    connect(&m_simTimer, &QTimer::timeout, this, &DroneSimulator::simulationStep);
}

DroneSimulator::~DroneSimulator()
{
    stop();
}

void DroneSimulator::start()
{
    m_elapsedTimer.start();
    m_state.boot_time_ms = 0;
    m_homeLat = m_state.latitude;
    m_homeLon = m_state.longitude;
    m_homeAlt = m_state.altitude;
    m_simTimer.start(SIM_INTERVAL_MS);
    qDebug() << "[DroneSimulator] シミュレーション開始";
}

void DroneSimulator::stop()
{
    m_simTimer.stop();
    qDebug() << "[DroneSimulator] シミュレーション停止";
}

// ============================================================
// コマンド
// ============================================================

bool DroneSimulator::arm()
{
    if (m_state.armed) {
        qDebug() << "[DroneSimulator] 既にArm済み";
        return true;
    }
    if (m_state.altitude > 0.5) {
        qDebug() << "[DroneSimulator] 飛行中はArmできません";
        return false;
    }
    m_state.armed = true;
    m_state.system_status = 4; // MAV_STATE_ACTIVE
    qDebug() << "[DroneSimulator] Armed";
    return true;
}

bool DroneSimulator::disarm()
{
    if (!m_state.armed) return true;
    if (m_state.altitude > 0.5) {
        qDebug() << "[DroneSimulator] 飛行中はDisarmできません";
        return false;
    }
    m_state.armed = false;
    m_state.system_status = 3; // MAV_STATE_STANDBY
    m_isTakingOff = false;
    m_isLanding = false;
    qDebug() << "[DroneSimulator] Disarmed";
    return true;
}

bool DroneSimulator::takeoff(double altitude)
{
    if (!m_state.armed) {
        qDebug() << "[DroneSimulator] Arm されていません";
        return false;
    }
    m_state.takeoff_altitude = altitude;
    m_state.flight_mode = FlightMode::GUIDED;
    m_isTakingOff = true;
    m_isLanding = false;
    qDebug() << "[DroneSimulator] Takeoff 高度:" << altitude << "m";
    return true;
}

bool DroneSimulator::land()
{
    if (!m_state.armed) return false;
    m_state.flight_mode = FlightMode::LAND;
    m_isLanding = true;
    m_isTakingOff = false;
    qDebug() << "[DroneSimulator] Landing開始";
    return true;
}

bool DroneSimulator::returnToLaunch()
{
    if (!m_state.armed) return false;
    m_state.flight_mode = FlightMode::RTL;
    m_state.target_latitude = m_homeLat;
    m_state.target_longitude = m_homeLon;
    m_state.target_altitude = 10.0; // RTL高度
    m_state.has_target = true;
    m_isTakingOff = false;
    m_isLanding = false;
    qDebug() << "[DroneSimulator] RTL開始";
    return true;
}

void DroneSimulator::setMode(FlightMode mode)
{
    m_state.flight_mode = mode;
    m_isTakingOff = false;
    m_isLanding = false;
    if (mode == FlightMode::LAND) {
        m_isLanding = true;
    }
    qDebug() << "[DroneSimulator] モード変更:" << m_state.flightModeName();
}

void DroneSimulator::setTargetPosition(double lat, double lon, double alt)
{
    m_state.target_latitude = lat;
    m_state.target_longitude = lon;
    m_state.target_altitude = alt;
    m_state.has_target = true;
    if (m_state.flight_mode != FlightMode::AUTO) {
        m_state.flight_mode = FlightMode::GUIDED;
    }
    qDebug() << "[DroneSimulator] ターゲット設定:" << lat << lon << alt;
}

void DroneSimulator::setManualInput(double roll, double pitch, double yaw, double throttle)
{
    m_inputRoll = qBound(-1.0, roll, 1.0);
    m_inputPitch = qBound(-1.0, pitch, 1.0);
    m_inputYaw = qBound(-1.0, yaw, 1.0);
    m_inputThrottle = qBound(-1.0, throttle, 1.0);
}

// ============================================================
// シミュレーションループ
// ============================================================

void DroneSimulator::simulationStep()
{
    double dt = SIM_INTERVAL_MS / 1000.0; // 秒
    m_state.boot_time_ms = static_cast<uint64_t>(m_elapsedTimer.elapsed());

    if (!m_state.armed) {
        // Disarm中: 状態のみ更新
        m_state.vx = 0; m_state.vy = 0; m_state.vz = 0;
        m_state.groundspeed = 0; m_state.airspeed = 0; m_state.climb_rate = 0;
        emit stateUpdated(m_state);
        return;
    }

    // フライトモードに応じた更新
    if (m_isTakingOff) {
        updateTakeoff(dt);
    } else if (m_isLanding || m_state.flight_mode == FlightMode::LAND) {
        updateLand(dt);
    } else if (m_state.flight_mode == FlightMode::GUIDED ||
               m_state.flight_mode == FlightMode::RTL ||
               m_state.flight_mode == FlightMode::AUTO) {
        updateGuided(dt);
    } else {
        updateManual(dt);
    }

    // バッテリー消耗シミュレーション
    updateBattery(dt);

    // 速度関連の計算
    updateVelocity(dt);

    // 高度が0以下にならないように制限
    if (m_state.altitude < 0.0) {
        m_state.altitude = 0.0;
        m_state.vz = 0.0;
        m_state.climb_rate = 0.0;
    }

    // 海抜高度 (簡易: 対地 + 50m)
    m_state.altitude_msl = m_state.altitude + 50.0;

    emit stateUpdated(m_state);
}

void DroneSimulator::updateTakeoff(double dt)
{
    double target = m_state.takeoff_altitude;
    double diff = target - m_state.altitude;

    if (diff > 0.1) {
        double rate = qMin(MAX_CLIMB_RATE, diff * 0.5);
        m_state.altitude += rate * dt;
        m_state.vz = -rate; // NED: 上昇は負
        m_state.climb_rate = rate;
    } else {
        m_state.altitude = target;
        m_state.vz = 0;
        m_state.climb_rate = 0;
        m_isTakingOff = false;
        m_state.flight_mode = FlightMode::GUIDED;
        qDebug() << "[DroneSimulator] Takeoff完了 高度:" << m_state.altitude << "m";
    }
}

void DroneSimulator::updateLand(double dt)
{
    if (m_state.altitude > 0.1) {
        double rate = qMin(MAX_DESCENT_RATE, m_state.altitude * 0.3);
        rate = qMax(rate, 0.2); // 最低降下速度
        m_state.altitude -= rate * dt;
        m_state.vz = rate; // NED: 降下は正
        m_state.climb_rate = -rate;
    } else {
        m_state.altitude = 0.0;
        m_state.vz = 0;
        m_state.climb_rate = 0;
        m_isLanding = false;
        disarm();
        qDebug() << "[DroneSimulator] 着陸完了";
    }
}

void DroneSimulator::updateGuided(double dt)
{
    if (!m_state.has_target) return;

    double dist = distanceBetween(m_state.latitude, m_state.longitude,
                                  m_state.target_latitude, m_state.target_longitude);

    if (dist > 0.5) {
        double bearing = bearingBetween(m_state.latitude, m_state.longitude,
                                        m_state.target_latitude, m_state.target_longitude);

        double speed = qMin(MAX_SPEED, dist * 0.5);
        double dlat = speed * qCos(bearing) * dt / 111320.0;
        double dlon = speed * qSin(bearing) * dt /
                      (111320.0 * qCos(qDegreesToRadians(m_state.latitude)));

        m_state.latitude += dlat;
        m_state.longitude += dlon;

        // ヨーをターゲット方向に向ける
        double targetYaw = bearing;
        double yawDiff = targetYaw - m_state.yaw;
        // 角度差を -π ~ π に正規化
        while (yawDiff > M_PI) yawDiff -= 2.0 * M_PI;
        while (yawDiff < -M_PI) yawDiff += 2.0 * M_PI;
        m_state.yaw += yawDiff * MOVE_SMOOTHING;

        m_state.vx = speed * qCos(bearing);
        m_state.vy = speed * qSin(bearing);
    } else {
        m_state.has_target = false;
        m_state.vx = 0; m_state.vy = 0;

        // RTLで到着したら着陸開始
        if (m_state.flight_mode == FlightMode::RTL) {
            land();
            return;
        }

        // AUTOモードではwaypointReachedを通知
        // MissionManagerが次のWPをセットする
        if (m_state.flight_mode == FlightMode::AUTO) {
            emit waypointReached();
            return;
        }
    }

    // 高度調整
    double altDiff = m_state.target_altitude - m_state.altitude;
    if (qAbs(altDiff) > 0.1) {
        double rate = qBound(-MAX_DESCENT_RATE, altDiff * 0.5, MAX_CLIMB_RATE);
        m_state.altitude += rate * dt;
        m_state.vz = -rate;
        m_state.climb_rate = rate;
    } else {
        m_state.vz = 0;
        m_state.climb_rate = 0;
    }
}

void DroneSimulator::updateManual(double dt)
{
    // ロール/ピッチ入力で水平移動
    double speed = MAX_SPEED;
    double moveX = -m_inputPitch * speed; // 前方 (North)
    double moveY = m_inputRoll * speed;    // 右方 (East)

    double dlat = moveX * dt / 111320.0;
    double dlon = moveY * dt / (111320.0 * qCos(qDegreesToRadians(m_state.latitude)));

    m_state.latitude += dlat;
    m_state.longitude += dlon;

    // ヨー
    m_state.yaw += qDegreesToRadians(m_inputYaw * YAW_RATE) * dt;
    while (m_state.yaw > M_PI) m_state.yaw -= 2.0 * M_PI;
    while (m_state.yaw < -M_PI) m_state.yaw += 2.0 * M_PI;

    // スロットルによる高度
    double climbRate = m_inputThrottle * MAX_CLIMB_RATE;
    m_state.altitude += climbRate * dt;
    m_state.vz = -climbRate;
    m_state.climb_rate = climbRate;

    // 姿勢の簡易反映
    m_state.roll = m_inputRoll * qDegreesToRadians(20.0);
    m_state.pitch = m_inputPitch * qDegreesToRadians(20.0);

    m_state.vx = moveX;
    m_state.vy = moveY;
}

void DroneSimulator::updateBattery(double dt)
{
    if (m_state.armed) {
        // 飛行中: 約20分でバッテリー消耗
        m_state.battery_remaining -= static_cast<float>(dt / (20.0 * 60.0) * 100.0);
        if (m_state.battery_remaining < 0) m_state.battery_remaining = 0;

        // 電圧: 残量に比例 (12.6V → 10.5V)
        m_state.battery_voltage = 10.5f + (m_state.battery_remaining / 100.0f) * 2.1f;

        // 電流: Arm時は基本消費
        m_state.battery_current = 5.0f + static_cast<float>(m_state.groundspeed) * 2.0f;
    } else {
        m_state.battery_current = 0.1f;
    }
}

void DroneSimulator::updateVelocity(double /*dt*/)
{
    m_state.groundspeed = qSqrt(m_state.vx * m_state.vx + m_state.vy * m_state.vy);
    m_state.airspeed = m_state.groundspeed; // 風なしの簡易モデル
}

// ============================================================
// ユーティリティ
// ============================================================

double DroneSimulator::distanceBetween(double lat1, double lon1, double lat2, double lon2) const
{
    // Haversine formula (簡易)
    double dlat = qDegreesToRadians(lat2 - lat1);
    double dlon = qDegreesToRadians(lon2 - lon1);
    double a = qSin(dlat / 2) * qSin(dlat / 2) +
               qCos(qDegreesToRadians(lat1)) * qCos(qDegreesToRadians(lat2)) *
               qSin(dlon / 2) * qSin(dlon / 2);
    double c = 2.0 * qAtan2(qSqrt(a), qSqrt(1.0 - a));
    return 6371000.0 * c; // 地球半径 * 角度 = メートル
}

double DroneSimulator::bearingBetween(double lat1, double lon1, double lat2, double lon2) const
{
    double dlon = qDegreesToRadians(lon2 - lon1);
    double rlat1 = qDegreesToRadians(lat1);
    double rlat2 = qDegreesToRadians(lat2);
    double y = qSin(dlon) * qCos(rlat2);
    double x = qCos(rlat1) * qSin(rlat2) - qSin(rlat1) * qCos(rlat2) * qCos(dlon);
    return qAtan2(y, x);
}
