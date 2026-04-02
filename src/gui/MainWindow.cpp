#include "MainWindow.h"
#include "AttitudeIndicator.h"
#include "TelemetryPanel.h"
#include "MapView.h"
#include "MapView3D.h"
#include "ControlPanel.h"
#include "core/DroneSimulator.h"
#include "core/FlightController.h"
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
#include <QDebug>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    // コンポーネント初期化
    m_simulator = new DroneSimulator(this);
    m_flightController = new FlightController(m_simulator, this);
    m_udpLink = new MavlinkUdpLink(this);
    m_mavManager = new MavlinkManager(m_udpLink, this);
    m_msgHandler = new MessageHandler(m_mavManager, m_flightController, this);

    setupStyle();
    setupUi();
    setupConnections();

    // UDP通信開始
    if (m_udpLink->start(14550, QHostAddress::LocalHost, 14550)) {
        qDebug() << "[MainWindow] UDP通信開始 ポート: 14550";
    }

    // シミュレーション＆テレメトリ開始
    m_simulator->start();
    m_mavManager->startTelemetry();

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
    )");
}

void MainWindow::setupUi()
{
    // ツールバー
    auto *toolbar = addToolBar("メイン");
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(16, 16));

    auto *actConnect = toolbar->addAction("🔌 接続");
    actConnect->setToolTip("UDP接続の再接続");
    connect(actConnect, &QAction::triggered, [this]() {
        m_udpLink->stop();
        if (m_udpLink->start(14550, QHostAddress::LocalHost, 14550)) {
            m_lblConnection->setText("● 接続中");
            m_lblConnection->setStyleSheet("color: #2ecc71;");
        }
    });

    toolbar->addSeparator();

    auto *actReset = toolbar->addAction("🔄 リセット");
    connect(actReset, &QAction::triggered, [this]() {
        m_simulator->stop();
        m_simulator->state() = DroneState();
        m_mapView->clearTrace();
        m_mapView3D->clearTrace();
        m_simulator->start();
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
    mapTabs->addTab(m_mapView3D, "🎮 3D ビュー");
    mapTabs->addTab(m_mapView, "🗺 2D マップ");

    // 右パネル（操作）
    m_controlPanel = new ControlPanel();
    m_controlPanel->setMinimumWidth(220);
    m_controlPanel->setMaximumWidth(280);

    mainSplitter->addWidget(leftWidget);
    mainSplitter->addWidget(mapTabs);
    mainSplitter->addWidget(m_controlPanel);
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

    m_lblConnection = new QLabel("● 待機中");
    m_lblConnection->setStyleSheet("color: #f39c12;");
    statusBar()->addPermanentWidget(m_lblConnection);
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
    }
}

void MainWindow::onDisarmRequested()
{
    if (!m_simulator->disarm()) {
        statusBar()->showMessage("DISARM失敗: 着陸してください", 3000);
    }
}

void MainWindow::onTakeoffRequested(double altitude)
{
    if (!m_simulator->takeoff(altitude)) {
        statusBar()->showMessage("TAKEOFF失敗: ARMしてください", 3000);
    }
}

void MainWindow::onLandRequested()
{
    m_simulator->land();
}

void MainWindow::onRtlRequested()
{
    m_simulator->returnToLaunch();
}

void MainWindow::onModeChanged(int modeIndex)
{
    auto *combo = m_controlPanel->findChild<QComboBox*>();
    if (combo) {
        uint32_t mode = combo->itemData(modeIndex).toUInt();
        m_flightController->setFlightMode(mode);
    }
}
