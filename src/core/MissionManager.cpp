#include "MissionManager.h"
#include "DroneSimulator.h"
#include <QDebug>

MissionManager::MissionManager(DroneSimulator *simulator, QObject *parent)
    : QObject(parent)
    , m_sim(simulator)
{
    // ウェイポイント到着通知を接続
    connect(m_sim, &DroneSimulator::waypointReached,
            this, &MissionManager::onWaypointReached);
}

// ============================================================
// ミッションアイテム操作
// ============================================================

void MissionManager::addItem(const MissionItem &item)
{
    MissionItem newItem = item;
    newItem.seq = static_cast<uint16_t>(m_items.size());
    m_items.append(newItem);
    emit itemsChanged();
}

void MissionManager::insertItem(int index, const MissionItem &item)
{
    if (index < 0 || index > m_items.size()) return;
    m_items.insert(index, item);
    renumberItems();
    emit itemsChanged();
}

void MissionManager::removeItem(int index)
{
    if (index < 0 || index >= m_items.size()) return;
    m_items.removeAt(index);
    renumberItems();
    emit itemsChanged();
}

void MissionManager::clearAll()
{
    m_items.clear();
    m_currentIndex = -1;
    m_running = false;
    m_paused = false;
    emit itemsChanged();
    emit statusMessage("ミッションクリア");
}

void MissionManager::setItems(const QVector<MissionItem> &items)
{
    m_items = items;
    renumberItems();
    emit itemsChanged();
}

void MissionManager::renumberItems()
{
    for (int i = 0; i < m_items.size(); i++) {
        m_items[i].seq = static_cast<uint16_t>(i);
    }
}

// ============================================================
// ミッション実行制御
// ============================================================

void MissionManager::startMission()
{
    if (m_items.isEmpty()) {
        emit statusMessage("ミッションアイテムがありません");
        return;
    }

    if (!m_sim->state().armed) {
        emit statusMessage("ARM されていません");
        return;
    }

    m_running = true;
    m_paused = false;
    m_currentIndex = 0;

    // AUTOモードに設定
    m_sim->setMode(FlightMode::AUTO);

    emit missionStarted();
    emit statusMessage(QString("ミッション開始 (%1 WP)").arg(m_items.size()));
    emit currentWaypointChanged(m_currentIndex);

    // 最初のアイテムを実行
    executeItem(m_items[m_currentIndex]);
}

void MissionManager::pauseMission()
{
    if (!m_running) return;
    m_paused = true;
    m_sim->setMode(FlightMode::LOITER);
    emit missionPaused();
    emit statusMessage("ミッション一時停止");
}

void MissionManager::resumeMission()
{
    if (!m_running || !m_paused) return;
    m_paused = false;
    m_sim->setMode(FlightMode::AUTO);

    // 現在のWPへの飛行を再開
    if (m_currentIndex >= 0 && m_currentIndex < m_items.size()) {
        executeItem(m_items[m_currentIndex]);
    }
    emit statusMessage("ミッション再開");
}

void MissionManager::stopMission()
{
    m_running = false;
    m_paused = false;
    m_currentIndex = -1;
    m_sim->setMode(FlightMode::LOITER);
    emit missionStopped();
    emit currentWaypointChanged(-1);
    emit statusMessage("ミッション中止");
}

void MissionManager::setCurrentWaypoint(int index)
{
    if (index < 0 || index >= m_items.size()) return;
    m_currentIndex = index;
    emit currentWaypointChanged(m_currentIndex);

    if (m_running && !m_paused) {
        executeItem(m_items[m_currentIndex]);
    }
}

// ============================================================
// WP到着処理
// ============================================================

void MissionManager::onWaypointReached()
{
    if (!m_running || m_paused) return;

    emit waypointReached(m_currentIndex);
    emit statusMessage(QString("WP #%1 到着").arg(m_currentIndex + 1));

    qDebug() << "[MissionManager] WP" << m_currentIndex << "到着";

    advanceToNextWaypoint();
}

void MissionManager::advanceToNextWaypoint()
{
    m_currentIndex++;

    if (m_currentIndex >= m_items.size()) {
        // ミッション完了
        onMissionComplete();
        return;
    }

    emit currentWaypointChanged(m_currentIndex);
    executeItem(m_items[m_currentIndex]);
}

void MissionManager::executeItem(const MissionItem &item)
{
    qDebug() << "[MissionManager] 実行: WP" << item.seq
             << item.commandName()
             << "lat:" << item.latitude
             << "lon:" << item.longitude
             << "alt:" << item.altitude;

    switch (item.command) {
    case MavCmd::NAV_WAYPOINT:
        // 通常のウェイポイント飛行
        m_sim->setTargetPosition(item.latitude, item.longitude, item.altitude);
        break;

    case MavCmd::NAV_TAKEOFF:
        // 離陸（高度のみ使用）
        m_sim->takeoff(item.altitude);
        break;

    case MavCmd::NAV_LAND:
        // 着陸
        m_sim->land();
        break;

    case MavCmd::NAV_RETURN_TO_LAUNCH:
        // RTL
        m_sim->returnToLaunch();
        break;

    case MavCmd::NAV_LOITER_UNLIM:
        // 無期限ホバリング（ミッション進行しない）
        m_sim->setTargetPosition(item.latitude, item.longitude, item.altitude);
        // autocontinue=0なので自動進行しない
        break;

    case MavCmd::NAV_LOITER_TIME: {
        // 一定時間ホバリング（param1=秒）
        m_sim->setTargetPosition(item.latitude, item.longitude, item.altitude);
        // TODO: タイマーで param1 秒後に次のWPへ進む
        // 現在は到着即次WPで簡易実装
        break;
    }

    default:
        qDebug() << "[MissionManager] 未対応コマンド:" << item.command;
        // 未対応コマンドはスキップ
        advanceToNextWaypoint();
        break;
    }

    emit statusMessage(QString("WP #%1 %2 実行中")
                       .arg(item.seq + 1)
                       .arg(item.commandName()));
}

void MissionManager::onMissionComplete()
{
    m_running = false;
    m_paused = false;

    qDebug() << "[MissionManager] ミッション完了";

    switch (m_completionAction) {
    case CompletionAction::RTL:
        m_sim->returnToLaunch();
        emit statusMessage("ミッション完了 → RTL");
        break;
    case CompletionAction::Land:
        m_sim->land();
        emit statusMessage("ミッション完了 → 着陸");
        break;
    case CompletionAction::Loiter:
    default:
        m_sim->setMode(FlightMode::LOITER);
        emit statusMessage("ミッション完了 → ホバリング");
        break;
    }

    emit missionCompleted();
    emit currentWaypointChanged(-1);
}
