#include "ControlPanel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFormLayout>

ControlPanel::ControlPanel(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
}

void ControlPanel::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(6);
    mainLayout->setContentsMargins(6, 6, 6, 6);

    QString groupStyle =
        "QGroupBox { color: #aaa; border: 1px solid #444; border-radius: 4px; "
        "margin-top: 8px; padding-top: 12px; font-weight: bold; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }";

    // === Arm / Disarm ===
    auto *armGroup = new QGroupBox("アーム制御");
    armGroup->setStyleSheet(groupStyle);
    auto *armLayout = new QHBoxLayout(armGroup);

    m_btnArm = new QPushButton("ARM");
    m_btnArm->setStyleSheet(buttonStyle("#e74c3c", "#c0392b"));
    m_btnArm->setMinimumHeight(36);
    connect(m_btnArm, &QPushButton::clicked, this, &ControlPanel::onArmClicked);

    m_btnDisarm = new QPushButton("DISARM");
    m_btnDisarm->setStyleSheet(buttonStyle("#2ecc71", "#27ae60"));
    m_btnDisarm->setMinimumHeight(36);
    connect(m_btnDisarm, &QPushButton::clicked, this, &ControlPanel::onDisarmClicked);

    armLayout->addWidget(m_btnArm);
    armLayout->addWidget(m_btnDisarm);
    mainLayout->addWidget(armGroup);

    // === 飛行コマンド ===
    auto *cmdGroup = new QGroupBox("飛行コマンド");
    cmdGroup->setStyleSheet(groupStyle);
    auto *cmdLayout = new QVBoxLayout(cmdGroup);

    // Takeoff高度設定
    auto *takeoffLayout = new QHBoxLayout();
    m_sliderTakeoffAlt = new QSlider(Qt::Horizontal);
    m_sliderTakeoffAlt->setRange(2, 100);
    m_sliderTakeoffAlt->setValue(10);
    m_sliderTakeoffAlt->setStyleSheet(
        "QSlider::groove:horizontal { background: #333; height: 6px; border-radius: 3px; }"
        "QSlider::handle:horizontal { background: #3498db; width: 14px; margin: -4px 0; border-radius: 7px; }"
        "QSlider::sub-page:horizontal { background: #2980b9; border-radius: 3px; }");
    m_lblTakeoffAlt = new QLabel("10 m");
    m_lblTakeoffAlt->setStyleSheet("color: #ccc; min-width: 40px;");
    connect(m_sliderTakeoffAlt, &QSlider::valueChanged, [this](int v) {
        m_lblTakeoffAlt->setText(QString::number(v) + " m");
    });
    takeoffLayout->addWidget(new QLabel("高度:"));
    takeoffLayout->addWidget(m_sliderTakeoffAlt);
    takeoffLayout->addWidget(m_lblTakeoffAlt);
    cmdLayout->addLayout(takeoffLayout);

    auto *btnLayout = new QHBoxLayout();
    m_btnTakeoff = new QPushButton("TAKEOFF");
    m_btnTakeoff->setStyleSheet(buttonStyle("#3498db", "#2980b9"));
    m_btnTakeoff->setMinimumHeight(36);
    connect(m_btnTakeoff, &QPushButton::clicked, this, &ControlPanel::onTakeoffClicked);

    m_btnLand = new QPushButton("LAND");
    m_btnLand->setStyleSheet(buttonStyle("#f39c12", "#e67e22"));
    m_btnLand->setMinimumHeight(36);
    connect(m_btnLand, &QPushButton::clicked, this, &ControlPanel::landRequested);

    m_btnRTL = new QPushButton("RTL");
    m_btnRTL->setStyleSheet(buttonStyle("#9b59b6", "#8e44ad"));
    m_btnRTL->setMinimumHeight(36);
    connect(m_btnRTL, &QPushButton::clicked, this, &ControlPanel::rtlRequested);

    btnLayout->addWidget(m_btnTakeoff);
    btnLayout->addWidget(m_btnLand);
    btnLayout->addWidget(m_btnRTL);
    cmdLayout->addLayout(btnLayout);
    mainLayout->addWidget(cmdGroup);

    // === フライトモード ===
    auto *modeGroup = new QGroupBox("フライトモード");
    modeGroup->setStyleSheet(groupStyle);
    auto *modeLayout = new QVBoxLayout(modeGroup);

    m_cmbMode = new QComboBox();
    m_cmbMode->addItem("STABILIZE", 0);
    m_cmbMode->addItem("GUIDED", 4);
    m_cmbMode->addItem("AUTO", 3);
    m_cmbMode->addItem("LOITER", 5);
    m_cmbMode->addItem("RTL", 6);
    m_cmbMode->addItem("LAND", 9);
    m_cmbMode->setStyleSheet(
        "QComboBox { background: #333; color: #ccc; border: 1px solid #555; "
        "border-radius: 3px; padding: 4px 8px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background: #333; color: #ccc; selection-background-color: #3498db; }");
    connect(m_cmbMode, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ControlPanel::modeChangeRequested);
    modeLayout->addWidget(m_cmbMode);
    mainLayout->addWidget(modeGroup);

    // === スロットル ===
    auto *throttleGroup = new QGroupBox("スロットル");
    throttleGroup->setStyleSheet(groupStyle);
    auto *throttleLayout = new QHBoxLayout(throttleGroup);

    m_sliderThrottle = new QSlider(Qt::Vertical);
    m_sliderThrottle->setRange(-100, 100);
    m_sliderThrottle->setValue(0);
    m_sliderThrottle->setMinimumHeight(100);
    m_sliderThrottle->setStyleSheet(
        "QSlider::groove:vertical { background: #333; width: 8px; border-radius: 4px; }"
        "QSlider::handle:vertical { background: #e74c3c; height: 16px; margin: 0 -4px; border-radius: 8px; }"
        "QSlider::sub-page:vertical { background: #555; border-radius: 4px; }"
        "QSlider::add-page:vertical { background: #2ecc71; border-radius: 4px; }");
    connect(m_sliderThrottle, &QSlider::valueChanged, this, &ControlPanel::onThrottleChanged);

    m_lblThrottleValue = new QLabel("0%");
    m_lblThrottleValue->setStyleSheet("color: #0f0; font-family: monospace; font-size: 14px;");

    throttleLayout->addWidget(m_sliderThrottle);
    throttleLayout->addWidget(m_lblThrottleValue);
    mainLayout->addWidget(throttleGroup);

    mainLayout->addStretch();

    // ラベルのスタイル
    for (auto *label : findChildren<QLabel*>()) {
        if (label->styleSheet().isEmpty()) {
            label->setStyleSheet("color: #999; font-size: 11px;");
        }
    }
}

QString ControlPanel::buttonStyle(const QString &baseColor, const QString &hoverColor) const
{
    return QString(
        "QPushButton { background: %1; color: white; border: none; border-radius: 4px; "
        "font-weight: bold; font-size: 13px; padding: 6px 12px; }"
        "QPushButton:hover { background: %2; }"
        "QPushButton:pressed { background: %2; padding-top: 8px; }"
    ).arg(baseColor, hoverColor);
}

void ControlPanel::onThrottleChanged(int value)
{
    m_lblThrottleValue->setText(QString::number(value) + "%");
    emit manualInputChanged(0, 0, 0, value / 100.0);
}

void ControlPanel::onArmClicked()
{
    emit armRequested();
}

void ControlPanel::onDisarmClicked()
{
    emit disarmRequested();
}

void ControlPanel::onTakeoffClicked()
{
    double alt = m_sliderTakeoffAlt->value();
    emit takeoffRequested(alt);
}
