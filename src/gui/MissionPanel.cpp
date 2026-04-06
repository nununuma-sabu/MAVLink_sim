#include "MissionPanel.h"
#include "core/MissionManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QHeaderView>

MissionPanel::MissionPanel(MissionManager *manager, QWidget *parent)
    : QWidget(parent)
    , m_manager(manager)
{
    setupUi();

    // MissionManager → GUI
    connect(m_manager, &MissionManager::itemsChanged,
            this, &MissionPanel::refreshList);
    connect(m_manager, &MissionManager::currentWaypointChanged,
            this, &MissionPanel::setActiveWaypoint);
    connect(m_manager, &MissionManager::statusMessage,
            this, &MissionPanel::showStatus);
}

void MissionPanel::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(6);
    mainLayout->setContentsMargins(6, 6, 6, 6);

    QString groupStyle =
        "QGroupBox { color: #aaa; border: 1px solid #444; border-radius: 4px; "
        "margin-top: 8px; padding-top: 12px; font-weight: bold; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }";

    // === ウェイポイントリスト ===
    auto *listGroup = new QGroupBox("ウェイポイント");
    listGroup->setStyleSheet(groupStyle);
    auto *listLayout = new QVBoxLayout(listGroup);

    m_listWidget = new QListWidget();
    m_listWidget->setStyleSheet(
        "QListWidget { background: #252530; color: #ccc; border: 1px solid #444; "
        "border-radius: 3px; font-family: monospace; font-size: 11px; }"
        "QListWidget::item { padding: 4px 8px; border-bottom: 1px solid #333; }"
        "QListWidget::item:selected { background: #2980b9; color: #fff; }"
        "QListWidget::item:hover { background: #35353e; }");
    m_listWidget->setMinimumHeight(120);
    connect(m_listWidget, &QListWidget::currentRowChanged,
            this, &MissionPanel::onItemSelected);
    listLayout->addWidget(m_listWidget);

    // 削除/クリアボタン
    auto *listBtnLayout = new QHBoxLayout();
    m_btnDelete = new QPushButton("削除");
    m_btnDelete->setStyleSheet(buttonStyle("#c0392b", "#a93226"));
    m_btnDelete->setMinimumHeight(28);
    connect(m_btnDelete, &QPushButton::clicked, this, &MissionPanel::onDeleteClicked);

    m_btnClear = new QPushButton("全クリア");
    m_btnClear->setStyleSheet(buttonStyle("#7f8c8d", "#6c7a7a"));
    m_btnClear->setMinimumHeight(28);
    connect(m_btnClear, &QPushButton::clicked, this, &MissionPanel::onClearClicked);

    listBtnLayout->addWidget(m_btnDelete);
    listBtnLayout->addWidget(m_btnClear);
    listLayout->addLayout(listBtnLayout);
    mainLayout->addWidget(listGroup);

    // === WP追加 ===
    auto *addGroup = new QGroupBox("ウェイポイント追加");
    addGroup->setStyleSheet(groupStyle);
    auto *addLayout = new QFormLayout(addGroup);
    addLayout->setSpacing(4);

    QString spinStyle =
        "QDoubleSpinBox { background: #333; color: #ccc; border: 1px solid #555; "
        "border-radius: 3px; padding: 2px 4px; }"
        "QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { "
        "background: #444; border: none; width: 16px; }"
        "QDoubleSpinBox::up-arrow { image: none; }"
        "QDoubleSpinBox::down-arrow { image: none; }";

    // コマンド選択
    m_cmbCommand = new QComboBox();
    m_cmbCommand->addItem("WAYPOINT",    MavCmd::NAV_WAYPOINT);
    m_cmbCommand->addItem("TAKEOFF",     MavCmd::NAV_TAKEOFF);
    m_cmbCommand->addItem("LAND",        MavCmd::NAV_LAND);
    m_cmbCommand->addItem("RTL",         MavCmd::NAV_RETURN_TO_LAUNCH);
    m_cmbCommand->addItem("LOITER_TIME", MavCmd::NAV_LOITER_TIME);
    m_cmbCommand->addItem("LOITER",      MavCmd::NAV_LOITER_UNLIM);
    m_cmbCommand->setStyleSheet(
        "QComboBox { background: #333; color: #ccc; border: 1px solid #555; "
        "border-radius: 3px; padding: 2px 6px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background: #333; color: #ccc; "
        "selection-background-color: #3498db; }");
    addLayout->addRow("コマンド:", m_cmbCommand);

    // 緯度
    m_spinLat = new QDoubleSpinBox();
    m_spinLat->setRange(-90.0, 90.0);
    m_spinLat->setDecimals(7);
    m_spinLat->setValue(35.6815);
    m_spinLat->setSingleStep(0.0001);
    m_spinLat->setStyleSheet(spinStyle);
    addLayout->addRow("緯度:", m_spinLat);

    // 経度
    m_spinLon = new QDoubleSpinBox();
    m_spinLon->setRange(-180.0, 180.0);
    m_spinLon->setDecimals(7);
    m_spinLon->setValue(139.7675);
    m_spinLon->setSingleStep(0.0001);
    m_spinLon->setStyleSheet(spinStyle);
    addLayout->addRow("経度:", m_spinLon);

    // 高度
    m_spinAlt = new QDoubleSpinBox();
    m_spinAlt->setRange(0.0, 500.0);
    m_spinAlt->setDecimals(1);
    m_spinAlt->setValue(20.0);
    m_spinAlt->setSingleStep(1.0);
    m_spinAlt->setSuffix(" m");
    m_spinAlt->setStyleSheet(spinStyle);
    addLayout->addRow("高度:", m_spinAlt);

    m_btnAdd = new QPushButton("追加");
    m_btnAdd->setStyleSheet(buttonStyle("#27ae60", "#219a52"));
    m_btnAdd->setMinimumHeight(32);
    connect(m_btnAdd, &QPushButton::clicked, this, &MissionPanel::onAddClicked);
    addLayout->addRow(m_btnAdd);

    // ラベルスタイル
    for (auto *label : addGroup->findChildren<QLabel*>()) {
        label->setStyleSheet("color: #999; font-size: 11px;");
    }
    mainLayout->addWidget(addGroup);

    // === 実行制御 ===
    auto *execGroup = new QGroupBox("ミッション実行");
    execGroup->setStyleSheet(groupStyle);
    auto *execLayout = new QVBoxLayout(execGroup);

    // 完了時動作
    auto *completionLayout = new QHBoxLayout();
    auto *lblCompletion = new QLabel("完了時:");
    lblCompletion->setStyleSheet("color: #999; font-size: 11px;");
    m_cmbCompletionAction = new QComboBox();
    m_cmbCompletionAction->addItem("ホバリング", 0);
    m_cmbCompletionAction->addItem("RTL (帰還)", 1);
    m_cmbCompletionAction->addItem("着陸", 2);
    m_cmbCompletionAction->setStyleSheet(
        "QComboBox { background: #333; color: #ccc; border: 1px solid #555; "
        "border-radius: 3px; padding: 2px 6px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background: #333; color: #ccc; "
        "selection-background-color: #3498db; }");
    connect(m_cmbCompletionAction, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [this](int index) {
        m_manager->setCompletionAction(
            static_cast<MissionManager::CompletionAction>(index));
    });
    completionLayout->addWidget(lblCompletion);
    completionLayout->addWidget(m_cmbCompletionAction, 1);
    execLayout->addLayout(completionLayout);

    // 実行ボタン
    auto *btnLayout = new QHBoxLayout();
    m_btnStart = new QPushButton("▶ 開始");
    m_btnStart->setStyleSheet(buttonStyle("#27ae60", "#219a52"));
    m_btnStart->setMinimumHeight(34);
    connect(m_btnStart, &QPushButton::clicked, this, &MissionPanel::onStartClicked);

    m_btnPause = new QPushButton("⏸ 一時停止");
    m_btnPause->setStyleSheet(buttonStyle("#f39c12", "#e67e22"));
    m_btnPause->setMinimumHeight(34);
    connect(m_btnPause, &QPushButton::clicked, this, &MissionPanel::onPauseClicked);

    m_btnStop = new QPushButton("⏹ 中止");
    m_btnStop->setStyleSheet(buttonStyle("#e74c3c", "#c0392b"));
    m_btnStop->setMinimumHeight(34);
    connect(m_btnStop, &QPushButton::clicked, this, &MissionPanel::onStopClicked);

    btnLayout->addWidget(m_btnStart);
    btnLayout->addWidget(m_btnPause);
    btnLayout->addWidget(m_btnStop);
    execLayout->addLayout(btnLayout);

    // 進捗表示
    m_lblProgress = new QLabel("WP: -- / --");
    m_lblProgress->setStyleSheet(
        "color: #3498db; font-family: monospace; font-size: 12px; font-weight: bold;");
    m_lblProgress->setAlignment(Qt::AlignCenter);
    execLayout->addWidget(m_lblProgress);

    mainLayout->addWidget(execGroup);

    // === ステータス ===
    m_lblStatus = new QLabel("準備完了");
    m_lblStatus->setStyleSheet(
        "color: #888; font-size: 10px; padding: 2px 4px; "
        "background: #1e1e23; border-radius: 2px;");
    m_lblStatus->setWordWrap(true);
    mainLayout->addWidget(m_lblStatus);

    mainLayout->addStretch();
}

QString MissionPanel::buttonStyle(const QString &baseColor, const QString &hoverColor) const
{
    return QString(
        "QPushButton { background: %1; color: white; border: none; border-radius: 4px; "
        "font-weight: bold; font-size: 11px; padding: 4px 8px; }"
        "QPushButton:hover { background: %2; }"
        "QPushButton:pressed { background: %2; padding-top: 6px; }"
    ).arg(baseColor, hoverColor);
}

// ============================================================
// スロット
// ============================================================

void MissionPanel::onAddClicked()
{
    MissionItem item;
    item.command = static_cast<uint16_t>(
        m_cmbCommand->currentData().toUInt());
    item.latitude = m_spinLat->value();
    item.longitude = m_spinLon->value();
    item.altitude = m_spinAlt->value();
    m_manager->addItem(item);
}

void MissionPanel::onDeleteClicked()
{
    int row = m_listWidget->currentRow();
    if (row >= 0) {
        m_manager->removeItem(row);
    }
}

void MissionPanel::onClearClicked()
{
    m_manager->clearAll();
}

void MissionPanel::onStartClicked()
{
    if (m_manager->isPaused()) {
        m_manager->resumeMission();
    } else {
        m_manager->startMission();
    }
}

void MissionPanel::onPauseClicked()
{
    m_manager->pauseMission();
}

void MissionPanel::onStopClicked()
{
    m_manager->stopMission();
}

void MissionPanel::onItemSelected(int row)
{
    if (row >= 0 && row < m_manager->itemCount()) {
        const auto &item = m_manager->items().at(row);
        m_spinLat->setValue(item.latitude);
        m_spinLon->setValue(item.longitude);
        m_spinAlt->setValue(item.altitude);
    }
}

void MissionPanel::addWaypointFromMap(double lat, double lon)
{
    MissionItem item;
    item.command = MavCmd::NAV_WAYPOINT;
    item.latitude = lat;
    item.longitude = lon;
    item.altitude = m_spinAlt->value(); // 現在設定中の高度を使用
    m_manager->addItem(item);
}

void MissionPanel::refreshList()
{
    m_listWidget->clear();
    const auto &items = m_manager->items();
    for (int i = 0; i < items.size(); i++) {
        const auto &item = items[i];
        QString text = QString("#%1  %2  %3, %4  %5m")
            .arg(i + 1, 2)
            .arg(item.commandName(), -10)
            .arg(item.latitude, 0, 'f', 5)
            .arg(item.longitude, 0, 'f', 5)
            .arg(item.altitude, 0, 'f', 1);
        m_listWidget->addItem(text);
    }

    m_lblProgress->setText(QString("WP: %1 / %2")
        .arg(m_manager->isRunning() ? m_manager->currentIndex() + 1 : 0)
        .arg(items.size()));
}

void MissionPanel::setActiveWaypoint(int index)
{
    for (int i = 0; i < m_listWidget->count(); i++) {
        auto *item = m_listWidget->item(i);
        if (i == index) {
            item->setBackground(QColor(41, 128, 185, 80));
            item->setForeground(QColor(255, 200, 0));
        } else {
            item->setBackground(Qt::transparent);
            item->setForeground(QColor(200, 200, 200));
        }
    }

    m_lblProgress->setText(QString("WP: %1 / %2")
        .arg(index >= 0 ? index + 1 : 0)
        .arg(m_manager->itemCount()));
}

void MissionPanel::showStatus(const QString &msg)
{
    m_lblStatus->setText(msg);
}
