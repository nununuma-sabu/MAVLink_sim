#include "TelemetryPanel.h"
#include <QtMath>
#include <QVBoxLayout>
#include <QGroupBox>

TelemetryPanel::TelemetryPanel(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
}

void TelemetryPanel::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(4);
    mainLayout->setContentsMargins(6, 6, 6, 6);

    // --- 飛行情報 ---
    auto *flightGroup = new QGroupBox("飛行情報");
    flightGroup->setStyleSheet(
        "QGroupBox { color: #aaa; border: 1px solid #444; border-radius: 4px; "
        "margin-top: 8px; padding-top: 12px; font-weight: bold; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }");
    auto *flightGrid = new QGridLayout(flightGroup);
    flightGrid->setSpacing(2);

    int row = 0;
    flightGrid->addWidget(createTitleLabel("高度 (AGL)"), row, 0);
    m_lblAltitude = createValueLabel();
    flightGrid->addWidget(m_lblAltitude, row++, 1);

    flightGrid->addWidget(createTitleLabel("高度 (MSL)"), row, 0);
    m_lblAltitudeMsl = createValueLabel();
    flightGrid->addWidget(m_lblAltitudeMsl, row++, 1);

    flightGrid->addWidget(createTitleLabel("対地速度"), row, 0);
    m_lblGroundSpeed = createValueLabel();
    flightGrid->addWidget(m_lblGroundSpeed, row++, 1);

    flightGrid->addWidget(createTitleLabel("対気速度"), row, 0);
    m_lblAirSpeed = createValueLabel();
    flightGrid->addWidget(m_lblAirSpeed, row++, 1);

    flightGrid->addWidget(createTitleLabel("昇降率"), row, 0);
    m_lblClimbRate = createValueLabel();
    flightGrid->addWidget(m_lblClimbRate, row++, 1);

    flightGrid->addWidget(createTitleLabel("ヘディング"), row, 0);
    m_lblHeading = createValueLabel();
    flightGrid->addWidget(m_lblHeading, row++, 1);

    mainLayout->addWidget(flightGroup);

    // --- GPS ---
    auto *gpsGroup = new QGroupBox("GPS");
    gpsGroup->setStyleSheet(flightGroup->styleSheet());
    auto *gpsGrid = new QGridLayout(gpsGroup);
    gpsGrid->setSpacing(2);
    row = 0;

    gpsGrid->addWidget(createTitleLabel("緯度"), row, 0);
    m_lblLatitude = createValueLabel();
    gpsGrid->addWidget(m_lblLatitude, row++, 1);

    gpsGrid->addWidget(createTitleLabel("経度"), row, 0);
    m_lblLongitude = createValueLabel();
    gpsGrid->addWidget(m_lblLongitude, row++, 1);

    gpsGrid->addWidget(createTitleLabel("Fix"), row, 0);
    m_lblGpsFix = createValueLabel();
    gpsGrid->addWidget(m_lblGpsFix, row++, 1);

    gpsGrid->addWidget(createTitleLabel("衛星数"), row, 0);
    m_lblSatellites = createValueLabel();
    gpsGrid->addWidget(m_lblSatellites, row++, 1);

    mainLayout->addWidget(gpsGroup);

    // --- 状態 ---
    auto *stateGroup = new QGroupBox("状態");
    stateGroup->setStyleSheet(flightGroup->styleSheet());
    auto *stateGrid = new QGridLayout(stateGroup);
    stateGrid->setSpacing(2);
    row = 0;

    stateGrid->addWidget(createTitleLabel("モード"), row, 0);
    m_lblFlightMode = createValueLabel();
    stateGrid->addWidget(m_lblFlightMode, row++, 1);

    stateGrid->addWidget(createTitleLabel("Arm"), row, 0);
    m_lblArmedState = createValueLabel();
    stateGrid->addWidget(m_lblArmedState, row++, 1);

    stateGrid->addWidget(createTitleLabel("飛行時間"), row, 0);
    m_lblFlightTime = createValueLabel();
    stateGrid->addWidget(m_lblFlightTime, row++, 1);

    mainLayout->addWidget(stateGroup);

    // --- バッテリー ---
    auto *battGroup = new QGroupBox("バッテリー");
    battGroup->setStyleSheet(flightGroup->styleSheet());
    auto *battLayout = new QVBoxLayout(battGroup);
    battLayout->setSpacing(2);

    m_lblBatteryVoltage = createValueLabel();
    battLayout->addWidget(m_lblBatteryVoltage);

    m_batteryBar = new QProgressBar();
    m_batteryBar->setRange(0, 100);
    m_batteryBar->setValue(100);
    m_batteryBar->setTextVisible(true);
    m_batteryBar->setStyleSheet(
        "QProgressBar { background: #333; border: 1px solid #555; border-radius: 3px; "
        "height: 16px; text-align: center; color: white; }"
        "QProgressBar::chunk { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, "
        "stop:0 #2ecc71, stop:1 #27ae60); border-radius: 2px; }");
    battLayout->addWidget(m_batteryBar);

    mainLayout->addWidget(battGroup);
    mainLayout->addStretch();
}

QLabel* TelemetryPanel::createValueLabel(const QString &initialText)
{
    auto *label = new QLabel(initialText);
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    label->setStyleSheet("color: #0f0; font-family: 'Consolas', 'Courier New', monospace; "
                         "font-size: 13px; font-weight: bold;");
    return label;
}

QLabel* TelemetryPanel::createTitleLabel(const QString &text)
{
    auto *label = new QLabel(text);
    label->setStyleSheet("color: #999; font-size: 11px;");
    return label;
}

void TelemetryPanel::updateTelemetry(const DroneState &state)
{
    m_lblAltitude->setText(QString::number(state.altitude, 'f', 1) + " m");
    m_lblAltitudeMsl->setText(QString::number(state.altitude_msl, 'f', 1) + " m");
    m_lblGroundSpeed->setText(QString::number(state.groundspeed, 'f', 1) + " m/s");
    m_lblAirSpeed->setText(QString::number(state.airspeed, 'f', 1) + " m/s");
    m_lblClimbRate->setText(QString::number(state.climb_rate, 'f', 1) + " m/s");

    double heading = qRadiansToDegrees(state.yaw);
    if (heading < 0) heading += 360.0;
    m_lblHeading->setText(QString::number(heading, 'f', 0) + "°");

    m_lblLatitude->setText(QString::number(state.latitude, 'f', 7));
    m_lblLongitude->setText(QString::number(state.longitude, 'f', 7));

    QString fixStr;
    switch (state.gps_fix_type) {
        case 0: fixStr = "No GPS"; break;
        case 1: fixStr = "No Fix"; break;
        case 2: fixStr = "2D Fix"; break;
        case 3: fixStr = "3D Fix"; break;
        default: fixStr = QString::number(state.gps_fix_type); break;
    }
    m_lblGpsFix->setText(fixStr);
    m_lblSatellites->setText(QString::number(state.satellites_visible));

    m_lblFlightMode->setText(state.flightModeName());
    m_lblFlightMode->setStyleSheet(
        state.armed ? "color: #f39c12; font-family: monospace; font-size: 13px; font-weight: bold;"
                    : "color: #0f0; font-family: monospace; font-size: 13px; font-weight: bold;");

    m_lblArmedState->setText(state.armed ? "ARMED" : "DISARMED");
    m_lblArmedState->setStyleSheet(
        state.armed ? "color: #e74c3c; font-family: monospace; font-size: 13px; font-weight: bold;"
                    : "color: #2ecc71; font-family: monospace; font-size: 13px; font-weight: bold;");

    // 飛行時間
    int secs = static_cast<int>(state.boot_time_ms / 1000);
    int mins = secs / 60;
    secs %= 60;
    m_lblFlightTime->setText(QString("%1:%2").arg(mins, 2, 10, QChar('0')).arg(secs, 2, 10, QChar('0')));

    // バッテリー
    m_lblBatteryVoltage->setText(QString::number(state.battery_voltage, 'f', 1) + " V / " +
                                  QString::number(state.battery_current, 'f', 1) + " A");
    m_batteryBar->setValue(static_cast<int>(state.battery_remaining));

    // バッテリー色
    QString chunkColor;
    if (state.battery_remaining > 50) {
        chunkColor = "stop:0 #2ecc71, stop:1 #27ae60";
    } else if (state.battery_remaining > 20) {
        chunkColor = "stop:0 #f39c12, stop:1 #e67e22";
    } else {
        chunkColor = "stop:0 #e74c3c, stop:1 #c0392b";
    }
    m_batteryBar->setStyleSheet(
        "QProgressBar { background: #333; border: 1px solid #555; border-radius: 3px; "
        "height: 16px; text-align: center; color: white; }"
        "QProgressBar::chunk { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, " +
        chunkColor + "); border-radius: 2px; }");
}
