#ifndef CONTROLPANEL_H
#define CONTROLPANEL_H

#include <QWidget>
#include <QPushButton>
#include <QSlider>
#include <QComboBox>
#include <QLabel>

/**
 * @brief 操作パネル
 *
 * Arm/Disarm、Takeoff/Land、フライトモード選択、
 * スロットル/方向入力を提供。
 */
class ControlPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ControlPanel(QWidget *parent = nullptr);

signals:
    void armRequested();
    void disarmRequested();
    void takeoffRequested(double altitude);
    void landRequested();
    void rtlRequested();
    void modeChangeRequested(int modeIndex);
    void manualInputChanged(double roll, double pitch, double yaw, double throttle);

private slots:
    void onThrottleChanged(int value);
    void onArmClicked();
    void onDisarmClicked();
    void onTakeoffClicked();

private:
    void setupUi();
    QString buttonStyle(const QString &baseColor, const QString &hoverColor) const;

    QPushButton *m_btnArm;
    QPushButton *m_btnDisarm;
    QPushButton *m_btnTakeoff;
    QPushButton *m_btnLand;
    QPushButton *m_btnRTL;
    QComboBox *m_cmbMode;
    QSlider *m_sliderThrottle;
    QLabel *m_lblThrottleValue;
    QSlider *m_sliderYaw;
    QLabel *m_lblTakeoffAlt;
    QSlider *m_sliderTakeoffAlt;
};

#endif // CONTROLPANEL_H
