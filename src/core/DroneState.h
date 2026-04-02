#ifndef DRONESTATE_H
#define DRONESTATE_H

#include <cstdint>
#include <QString>

/**
 * @brief フライトモード定義
 */
enum class FlightMode : uint8_t {
    STABILIZE = 0,
    GUIDED    = 4,
    AUTO      = 3,
    LAND      = 9,
    RTL       = 6,
    LOITER    = 5
};

/**
 * @brief ドローンの全状態を保持する構造体
 *
 * 位置ベースのシミュレーションで使用。
 * 座標系: WGS84 (緯度/経度) + 高度(m)
 * 姿勢: ラジアン (NED座標系)
 */
struct DroneState {
    // === 位置 (WGS84) ===
    double latitude  = 35.6812;   // 緯度 [deg] (デフォルト: 東京)
    double longitude = 139.7671;  // 経度 [deg]
    double altitude  = 0.0;       // 高度 [m] (対地)
    double altitude_msl = 0.0;    // 高度 [m] (海抜)

    // === 姿勢 [rad] ===
    double roll  = 0.0;
    double pitch = 0.0;
    double yaw   = 0.0;

    // === 速度 (NED座標系, m/s) ===
    double vx = 0.0;  // 北方向
    double vy = 0.0;  // 東方向
    double vz = 0.0;  // 下方向

    // === 対地速度 / 対気速度 ===
    double groundspeed = 0.0;  // m/s
    double airspeed    = 0.0;  // m/s
    double climb_rate  = 0.0;  // m/s (正=上昇)

    // === バッテリー ===
    float battery_voltage   = 12.6f;  // V (3S LiPo)
    float battery_current   = 0.0f;   // A
    float battery_remaining = 100.0f; // % (0-100)

    // === システム状態 ===
    bool armed = false;
    FlightMode flight_mode = FlightMode::STABILIZE;
    uint8_t system_status = 3; // MAV_STATE_STANDBY

    // === GPS ===
    uint8_t gps_fix_type = 3;      // GPS_FIX_TYPE_3D_FIX
    int satellites_visible = 12;

    // === タイムスタンプ ===
    uint64_t boot_time_ms = 0;  // 起動からの経過時間 [ms]

    // === ターゲット位置 (GUIDEDモード用) ===
    double target_latitude  = 0.0;
    double target_longitude = 0.0;
    double target_altitude  = 0.0;
    bool has_target = false;

    // === Takeoff目標高度 ===
    double takeoff_altitude = 10.0; // m

    /**
     * @brief フライトモード名を文字列で取得
     */
    QString flightModeName() const {
        switch (flight_mode) {
            case FlightMode::STABILIZE: return "STABILIZE";
            case FlightMode::GUIDED:    return "GUIDED";
            case FlightMode::AUTO:      return "AUTO";
            case FlightMode::LAND:      return "LAND";
            case FlightMode::RTL:       return "RTL";
            case FlightMode::LOITER:    return "LOITER";
            default: return "UNKNOWN";
        }
    }
};

#endif // DRONESTATE_H
