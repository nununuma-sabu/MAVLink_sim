#ifndef MISSIONPANEL_H
#define MISSIONPANEL_H

#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QLabel>

class MissionManager;
struct MissionItem;

/**
 * @brief ミッション計画パネル
 *
 * ウェイポイントの追加・編集・削除、ミッション実行制御を提供。
 */
class MissionPanel : public QWidget
{
    Q_OBJECT

public:
    explicit MissionPanel(MissionManager *manager, QWidget *parent = nullptr);

    /// 外部からWP追加（マップクリック等）
    void addWaypointFromMap(double lat, double lon);

public slots:
    /// ミッションアイテムリストの再描画
    void refreshList();
    /// 現在のWPハイライト更新
    void setActiveWaypoint(int index);
    /// ステータスメッセージ表示
    void showStatus(const QString &msg);

private slots:
    void onAddClicked();
    void onDeleteClicked();
    void onClearClicked();
    void onStartClicked();
    void onPauseClicked();
    void onStopClicked();
    void onItemSelected(int row);

private:
    void setupUi();
    QString buttonStyle(const QString &baseColor, const QString &hoverColor) const;

    MissionManager *m_manager;

    // UI要素
    QListWidget    *m_listWidget;
    QDoubleSpinBox *m_spinLat;
    QDoubleSpinBox *m_spinLon;
    QDoubleSpinBox *m_spinAlt;
    QComboBox      *m_cmbCommand;
    QComboBox      *m_cmbCompletionAction;
    QPushButton    *m_btnAdd;
    QPushButton    *m_btnDelete;
    QPushButton    *m_btnClear;
    QPushButton    *m_btnStart;
    QPushButton    *m_btnPause;
    QPushButton    *m_btnStop;
    QLabel         *m_lblStatus;
    QLabel         *m_lblProgress;
};

#endif // MISSIONPANEL_H
