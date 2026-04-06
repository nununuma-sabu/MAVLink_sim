#ifndef MISSIONMANAGER_H
#define MISSIONMANAGER_H

#include <QObject>
#include <QVector>
#include <QString>

/**
 * @brief ミッションアイテム構造体
 *
 * MAVLink MISSION_ITEM_INT 互換のウェイポイントデータ。
 */
struct MissionItem {
    uint16_t seq         = 0;       // シーケンス番号
    uint16_t command     = 16;      // MAV_CMD (デフォルト: NAV_WAYPOINT)
    uint8_t  frame       = 3;       // MAV_FRAME_GLOBAL_RELATIVE_ALT
    uint8_t  current     = 0;       // 現在の目標WP
    uint8_t  autocontinue = 1;      // 自動遷移
    float    param1      = 0.0f;    // Hold time (sec) / 他
    float    param2      = 0.0f;    // Accept radius (m)
    float    param3      = 0.0f;    // Pass through (0) / orbit
    float    param4      = 0.0f;    // Yaw angle (deg)
    double   latitude    = 0.0;     // 緯度 [deg]
    double   longitude   = 0.0;     // 経度 [deg]
    double   altitude    = 0.0;     // 高度 [m] (相対)

    /**
     * @brief コマンド名を文字列で取得
     */
    QString commandName() const {
        switch (command) {
            case 16: return "WAYPOINT";
            case 17: return "LOITER_UNLIM";
            case 18: return "LOITER_TURNS";
            case 19: return "LOITER_TIME";
            case 20: return "RTL";
            case 21: return "LAND";
            case 22: return "TAKEOFF";
            default: return QString("CMD_%1").arg(command);
        }
    }
};

// MAV_CMD 定数
namespace MavCmd {
    static constexpr uint16_t NAV_WAYPOINT    = 16;
    static constexpr uint16_t NAV_LOITER_UNLIM = 17;
    static constexpr uint16_t NAV_LOITER_TURNS = 18;
    static constexpr uint16_t NAV_LOITER_TIME  = 19;
    static constexpr uint16_t NAV_RETURN_TO_LAUNCH = 20;
    static constexpr uint16_t NAV_LAND         = 21;
    static constexpr uint16_t NAV_TAKEOFF      = 22;
}

class DroneSimulator;

/**
 * @brief ミッション管理クラス
 *
 * ウェイポイントリストの管理とAUTOモードでの順次飛行実行。
 * MAVLinkミッションプロトコルのアイテムストアも兼ねる。
 */
class MissionManager : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief ミッション完了時の動作
     */
    enum class CompletionAction : int {
        Loiter = 0,   // 最終WPでホバリング
        RTL    = 1,   // ホームに帰還
        Land   = 2    // その場で着陸
    };

    explicit MissionManager(DroneSimulator *simulator, QObject *parent = nullptr);

    // === ミッションアイテム操作 ===
    void addItem(const MissionItem &item);
    void insertItem(int index, const MissionItem &item);
    void removeItem(int index);
    void clearAll();
    void setItems(const QVector<MissionItem> &items);

    const QVector<MissionItem>& items() const { return m_items; }
    int itemCount() const { return m_items.size(); }
    int currentIndex() const { return m_currentIndex; }

    // === ミッション実行制御 ===
    void startMission();
    void pauseMission();
    void resumeMission();
    void stopMission();
    void setCurrentWaypoint(int index);

    bool isRunning() const { return m_running; }
    bool isPaused() const { return m_paused; }

    // === 完了時動作 ===
    void setCompletionAction(CompletionAction action) { m_completionAction = action; }
    CompletionAction completionAction() const { return m_completionAction; }

signals:
    /// ミッションリスト変更
    void itemsChanged();
    /// 現在のWPインデックス変更
    void currentWaypointChanged(int index);
    /// WP到着
    void waypointReached(int index);
    /// ミッション開始
    void missionStarted();
    /// ミッション一時停止
    void missionPaused();
    /// ミッション完了
    void missionCompleted();
    /// ミッション中止
    void missionStopped();
    /// ステータスメッセージ
    void statusMessage(const QString &msg);

public slots:
    /// DroneSimulator::waypointReached から呼ばれる
    void onWaypointReached();

private:
    void advanceToNextWaypoint();
    void executeItem(const MissionItem &item);
    void onMissionComplete();
    void renumberItems();

    DroneSimulator *m_sim;
    QVector<MissionItem> m_items;
    int m_currentIndex = -1;
    bool m_running = false;
    bool m_paused = false;
    CompletionAction m_completionAction = CompletionAction::Loiter;
};

#endif // MISSIONMANAGER_H
