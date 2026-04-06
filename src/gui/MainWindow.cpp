#include "MainWindow.h"
#include "AttitudeIndicator.h"
#include "TelemetryPanel.h"
#include "MapView.h"
#include "MapView3D.h"
#include "ControlPanel.h"
#include "LogPanel.h"
#include "MissionPanel.h"
#include "core/DroneSimulator.h"
#include "core/FlightController.h"
#include "core/MissionManager.h"
#include "mavlink/MavlinkUdpLink.h"
#include "mavlink/MavlinkManager.h"
#include "mavlink/MessageHandler.h"

#include <QSplitter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStatusBar>
#include <QToolBar>
#include <QAction>
#include <QMessageBox>
#include <QComboBox>
#include <QTabWidget>
#include <QDockWidget>
#include <QDebug>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    // コンポーネント初期化
    m_simulator = new DroneSimulator(this);
    m_flightController = new FlightController(m_simulator, this);
    m_missionManager = new MissionManager(m_simulator, this);
    m_udpLink = new MavlinkUdpLink(this);
    m_mavManager = new MavlinkManager(m_udpLink, this);
    m_msgHandler = new MessageHandler(m_mavManager, m_flightController, m_missionManager, this);

    setupStyle();
    setupUi();
    setupConnections();

    // UDP通信開始 (ローカル: 14540, 送信先: 14550)
    // QGCは14550でリッスンするので、シミュレータは14540でリッスン
    if (m_udpLink->start(14540, QHostAddress::LocalHost, 14550)) {
        qDebug() << "[MainWindow] UDP通信開始 ローカル:14540 → GCS:14550";
        m_logPanel->appendLog("UDP通信開始 ローカル:14540 → GCS:14550");
    }

    // シミュレーション＆テレメトリ開始
    m_simulator->start();
    m_mavManager->startTelemetry();

    // GCS Heartbeat タイムアウト設定 (5秒)
    m_gcsTimeoutTimer.setSingleShot(true);
    connect(&m_gcsTimeoutTimer, &QTimer::timeout, [this]() {
        m_lblGcsStatus->setText("GCS: 未接続");
        m_lblGcsStatus->setStyleSheet("color: #888; font-size: 11px;");
        m_logPanel->appendLog("GCS Heartbeat タイムアウト — 未接続");
    });

    setWindowTitle("MAVLink ドローンシミュレーター v1.0");
    resize(1200, 800);
}

MainWindow::~MainWindow()
{
    m_mavManager->stopTelemetry();
    m_simulator->stop();
    m_udpLink->stop();
}

void MainWindow::setupStyle()
{
    setStyleSheet(R"(
        QMainWindow {
            background: #1e1e23;
        }
        QToolBar {
            background: #2a2a32;
            border-bottom: 1px solid #3a3a42;
            spacing: 6px;
            padding: 4px;
        }
        QToolBar QToolButton {
            background: transparent;
            color: #ccc;
            border: 1px solid transparent;
            border-radius: 4px;
            padding: 4px 10px;
            font-size: 12px;
        }
        QToolBar QToolButton:hover {
            background: #3a3a45;
            border-color: #555;
        }
        QStatusBar {
            background: #252530;
            color: #888;
            border-top: 1px solid #3a3a42;
            font-size: 11px;
        }
        QSplitter::handle {
            background: #3a3a42;
            width: 2px;
            height: 2px;
        }
        QScrollBar:vertical {
            background: #2a2a32;
            width: 8px;
            border: none;
        }
        QScrollBar::handle:vertical {
            background: #555;
            border-radius: 4px;
            min-height: 20px;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0;
        }
        QDockWidget {
            color: #ccc;
            titlebar-close-icon: none;
        }
        QDockWidget::title {
            background: #252530;
            padding: 4px;
            border-bottom: 1px solid #3a3a42;
        }
    )");
}

void MainWindow::setupUi()
{
    // ツールバー
    auto *toolbar = addToolBar("メイン");
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(16, 16));

    auto *actConnect = toolbar->addAction("接続");
    actConnect->setToolTip("UDP接続の再接続");
    connect(actConnect, &QAction::triggered, [this]() {
        m_udpLink->stop();
        if (m_udpLink->start(14540, QHostAddress::LocalHost, 14550)) {
            m_lblConnection->setText("● 接続中");
            m_lblConnection->setStyleSheet("color: #2ecc71;");
            m_logPanel->appendLog("UDP再接続 ローカル:14540 → GCS:14550");
        }
    });

    toolbar->addSeparator();

    auto *actReset = toolbar->addAction("リセット");
    connect(actReset, &QAction::triggered, [this]() {
        m_simulator->stop();
        m_simulator->state() = DroneState();
        m_mapView->clearTrace();
        m_mapView3D->clearTrace();
        m_simulator->start();
        m_logPanel->appendLog("シミュレーションリセット");
    });

    // メインレイアウト
    auto *centralWidget = new QWidget();
    setCentralWidget(centralWidget);

    auto *mainSplitter = new QSplitter(Qt::Horizontal);

    // 左パネル（姿勢表示 + テレメトリ）
    auto *leftWidget = new QWidget();
    auto *leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setSpacing(4);
    leftLayout->setContentsMargins(4, 4, 4, 4);

    m_attitudeIndicator = new AttitudeIndicator();
    leftLayout->addWidget(m_attitudeIndicator, 0);

    m_telemetryPanel = new TelemetryPanel();
    leftLayout->addWidget(m_telemetryPanel, 1);

    leftWidget->setMinimumWidth(250);
    leftWidget->setMaximumWidth(320);

    // 中央パネル（2D/3D切替タブ）
    auto *mapTabs = new QTabWidget();
    mapTabs->setStyleSheet(
        "QTabWidget::pane { border: 1px solid #3a3a42; background: #1e1e23; }"
        "QTabBar::tab { background: #2a2a32; color: #999; padding: 6px 16px; "
        "border: 1px solid #3a3a42; border-bottom: none; border-top-left-radius: 4px; "
        "border-top-right-radius: 4px; margin-right: 2px; }"
        "QTabBar::tab:selected { background: #1e1e23; color: #fff; }"
        "QTabBar::tab:hover { background: #35353e; }");

    m_mapView3D = new MapView3D();
    m_mapView = new MapView();
    mapTabs->addTab(m_mapView3D, "3D ビュー");
    mapTabs->addTab(m_mapView, "2D マップ");

    // 右パネル（操作 / ミッション タブ）
    m_controlPanel = new ControlPanel();
    m_missionPanel = new MissionPanel(m_missionManager);

    auto *rightTabs = new QTabWidget();
    rightTabs->setStyleSheet(
        "QTabWidget::pane { border: 1px solid #3a3a42; background: #1e1e23; }"
        "QTabBar::tab { background: #2a2a32; color: #999; padding: 6px 16px; "
        "border: 1px solid #3a3a42; border-bottom: none; border-top-left-radius: 4px; "
        "border-top-right-radius: 4px; margin-right: 2px; }"
        "QTabBar::tab:selected { background: #1e1e23; color: #fff; }"
        "QTabBar::tab:hover { background: #35353e; }");
    rightTabs->addTab(m_controlPanel, "操作");
    rightTabs->addTab(m_missionPanel, "ミッション");
    rightTabs->setMinimumWidth(240);
    rightTabs->setMaximumWidth(320);

    mainSplitter->addWidget(leftWidget);
    mainSplitter->addWidget(mapTabs);
    mainSplitter->addWidget(rightTabs);
    mainSplitter->setStretchFactor(0, 0);
    mainSplitter->setStretchFactor(1, 1);
    mainSplitter->setStretchFactor(2, 0);

    auto *layout = new QVBoxLayout(centralWidget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(mainSplitter);

    // ステータスバー
    m_lblStatus = new QLabel("DISARMED | STABILIZE");
    m_lblStatus->setStyleSheet("color: #2ecc71; font-weight: bold;");
    statusBar()->addWidget(m_lblStatus, 1);

    m_lblGcsStatus = new QLabel("GCS: 未接続");
    m_lblGcsStatus->setStyleSheet("color: #888; font-size: 11px;");
    statusBar()->addPermanentWidget(m_lblGcsStatus);

    m_lblConnection = new QLabel("● 待機中");
    m_lblConnection->setStyleSheet("color: #f39c12;");
    statusBar()->addPermanentWidget(m_lblConnection);

    // ログパネル（ドッキングウィジェット）
    m_logPanel = new LogPanel();
    auto *dockLog = new QDockWidget("MAVLink ログ", this);
    dockLog->setWidget(m_logPanel);
    dockLog->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
    dockLog->setMinimumHeight(120);
    addDockWidget(Qt::BottomDockWidgetArea, dockLog);

    // ツールバーにログ表示/非表示トグルを追加
    auto *actLog = dockLog->toggleViewAction();
    actLog->setText("ログ");
    toolbar->addSeparator();
    toolbar->addAction(actLog);
}

void MainWindow::setupConnections()
{
    // シミュレーション状態更新 → GUI更新 + MAVLink送信
    connect(m_simulator, &DroneSimulator::stateUpdated,
            this, &MainWindow::onStateUpdated);

    // UDP受信 → メッセージハンドラ
    connect(m_udpLink, &MavlinkUdpLink::messageReceived,
            m_msgHandler, &MessageHandler::handleMessage);

    // 接続状態
    connect(m_udpLink, &MavlinkUdpLink::connectionChanged, [this](bool connected) {
        if (connected) {
            m_lblConnection->setText("● 接続中");
            m_lblConnection->setStyleSheet("color: #2ecc71;");
        } else {
            m_lblConnection->setText("● 切断");
            m_lblConnection->setStyleSheet("color: #e74c3c;");
        }
    });

    // GCS Heartbeat 受信 → ステータス更新
    connect(m_msgHandler, &MessageHandler::gcsHeartbeatReceived, [this]() {
        m_lblGcsStatus->setText("GCS: 接続済み ✓");
        m_lblGcsStatus->setStyleSheet("color: #2ecc71; font-size: 11px; font-weight: bold;");
        // タイムアウトタイマーをリスタート (5秒)
        m_gcsTimeoutTimer.start(5000);
    });

    // ログ出力
    connect(m_msgHandler, &MessageHandler::logMessage,
            m_logPanel, &LogPanel::appendLog);

    // 操作パネル → シミュレーター
    connect(m_controlPanel, &ControlPanel::armRequested,
            this, &MainWindow::onArmRequested);
    connect(m_controlPanel, &ControlPanel::disarmRequested,
            this, &MainWindow::onDisarmRequested);
    connect(m_controlPanel, &ControlPanel::takeoffRequested,
            this, &MainWindow::onTakeoffRequested);
    connect(m_controlPanel, &ControlPanel::landRequested,
            this, &MainWindow::onLandRequested);
    connect(m_controlPanel, &ControlPanel::rtlRequested,
            this, &MainWindow::onRtlRequested);
    connect(m_controlPanel, &ControlPanel::modeChangeRequested,
            this, &MainWindow::onModeChanged);
    connect(m_controlPanel, &ControlPanel::manualInputChanged,
            m_simulator, &DroneSimulator::setManualInput);

    // === ミッション関連 ===

    // MissionManager → マップ表示更新
    connect(m_missionManager, &MissionManager::itemsChanged, [this]() {
        m_mapView->setWaypoints(m_missionManager->items());
        m_mapView3D->setWaypoints(m_missionManager->items());
    });
    connect(m_missionManager, &MissionManager::currentWaypointChanged, [this](int index) {
        m_mapView->setActiveWaypoint(index);
        m_mapView3D->setActiveWaypoint(index);
    });

    // MissionManager → MAVLink通知
    connect(m_missionManager, &MissionManager::currentWaypointChanged, [this](int index) {
        if (index >= 0) {
            m_mavManager->sendMissionCurrent(static_cast<uint16_t>(index));
        }
    });
    connect(m_missionManager, &MissionManager::waypointReached, [this](int index) {
        m_mavManager->sendMissionItemReached(static_cast<uint16_t>(index));
        m_logPanel->appendLog(QString("→ MISSION_ITEM_REACHED seq:%1").arg(index));
    });
    connect(m_missionManager, &MissionManager::statusMessage,
            m_logPanel, &LogPanel::appendLog);

    // マップクリック → MissionPanel にWP追加
    connect(m_mapView, &MapView::waypointAddRequested, [this](double lat, double lon) {
        m_missionPanel->addWaypointFromMap(lat, lon);
        m_logPanel->appendLog(QString("マップクリック WP追加: %1, %2")
                              .arg(lat, 0, 'f', 5).arg(lon, 0, 'f', 5));
    });
}

void MainWindow::onStateUpdated(const DroneState &state)
{
    // MAVLinkへ状態転送
    m_mavManager->updateState(state);

    // GUI更新 (10フレームに1回に間引き - 100Hz→10Hz)
    static int counter = 0;
    if (++counter % 10 != 0) return;

    m_attitudeIndicator->setAttitude(state.roll, state.pitch, state.yaw);
    m_telemetryPanel->updateTelemetry(state);
    m_mapView->updatePosition(state.latitude, state.longitude, state.yaw);
    m_mapView3D->updateDrone(state.latitude, state.longitude, state.altitude,
                              state.roll, state.pitch, state.yaw);

    // ステータスバー
    QString armStr = state.armed ? "ARMED" : "DISARMED";
    m_lblStatus->setText(armStr + " | " + state.flightModeName());
    m_lblStatus->setStyleSheet(
        state.armed ? "color: #e74c3c; font-weight: bold;"
                    : "color: #2ecc71; font-weight: bold;");
}

void MainWindow::onArmRequested()
{
    if (!m_simulator->arm()) {
        statusBar()->showMessage("ARM失敗: 地上にいることを確認してください", 3000);
    } else {
        m_logPanel->appendLog("→ ARM 実行");
    }
}

void MainWindow::onDisarmRequested()
{
    if (!m_simulator->disarm()) {
        statusBar()->showMessage("DISARM失敗: 着陸してください", 3000);
    } else {
        m_logPanel->appendLog("→ DISARM 実行");
    }
}

void MainWindow::onTakeoffRequested(double altitude)
{
    if (!m_simulator->takeoff(altitude)) {
        statusBar()->showMessage("TAKEOFF失敗: ARMしてください", 3000);
    } else {
        m_logPanel->appendLog(QString("→ TAKEOFF 高度: %1m").arg(altitude));
    }
}

void MainWindow::onLandRequested()
{
    m_simulator->land();
    m_logPanel->appendLog("→ LAND 実行");
}

void MainWindow::onRtlRequested()
{
    m_simulator->returnToLaunch();
    m_logPanel->appendLog("→ RTL 実行");
}

void MainWindow::onModeChanged(int modeIndex)
{
    auto *combo = m_controlPanel->findChild<QComboBox*>();
    if (combo) {
        uint32_t mode = combo->itemData(modeIndex).toUInt();
        m_flightController->setFlightMode(mode);
        m_logPanel->appendLog(QString("→ モード変更: %1").arg(combo->itemText(modeIndex)));
    }
}
