#ifndef ATTITUDEINDICATOR_H
#define ATTITUDEINDICATOR_H

#include <QWidget>

/**
 * @brief 人工水平儀（Attitude Indicator）ウィジェット
 *
 * QPainterによる航空計器風の姿勢表示。
 * ロール・ピッチ・ヘディングを視覚化。
 */
class AttitudeIndicator : public QWidget
{
    Q_OBJECT

public:
    explicit AttitudeIndicator(QWidget *parent = nullptr);

    void setAttitude(double roll, double pitch, double yaw);

    QSize minimumSizeHint() const override { return QSize(200, 200); }
    QSize sizeHint() const override { return QSize(280, 280); }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void drawSky(QPainter &p, int cx, int cy, int radius);
    void drawPitchScale(QPainter &p, int cx, int cy, int radius);
    void drawRollPointer(QPainter &p, int cx, int cy, int radius);
    void drawAircraftSymbol(QPainter &p, int cx, int cy);
    void drawHeadingBar(QPainter &p, int x, int y, int width);
    void drawBorder(QPainter &p, int cx, int cy, int radius);

    double m_roll  = 0.0;  // rad
    double m_pitch = 0.0;  // rad
    double m_yaw   = 0.0;  // rad
};

#endif // ATTITUDEINDICATOR_H
