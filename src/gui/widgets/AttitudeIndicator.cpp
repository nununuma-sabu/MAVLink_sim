#include "AttitudeIndicator.h"
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

AttitudeIndicator::AttitudeIndicator(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(200, 200);
}

void AttitudeIndicator::setAttitude(double roll, double pitch, double yaw)
{
    m_roll = roll;
    m_pitch = pitch;
    m_yaw = yaw;
    update();
}

void AttitudeIndicator::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    int side = qMin(width(), height() - 30); // ヘディングバー分を確保
    int radius = side / 2 - 5;
    int cx = width() / 2;
    int cy = 15 + side / 2;

    // 背景
    p.fillRect(rect(), QColor(30, 30, 35));

    // クリッピング（円形）
    QPainterPath clipPath;
    clipPath.addEllipse(QPoint(cx, cy), radius, radius);
    p.setClipPath(clipPath);

    // 空と地面
    drawSky(p, cx, cy, radius);

    // ピッチスケール
    drawPitchScale(p, cx, cy, radius);

    p.setClipping(false);

    // ロールポインター
    drawRollPointer(p, cx, cy, radius);

    // 機体シンボル
    drawAircraftSymbol(p, cx, cy);

    // 外枠
    drawBorder(p, cx, cy, radius);

    // ヘディングバー
    drawHeadingBar(p, 10, height() - 25, width() - 20);
}

void AttitudeIndicator::drawSky(QPainter &p, int cx, int cy, int radius)
{
    p.save();
    p.translate(cx, cy);
    p.rotate(qRadiansToDegrees(-m_roll));

    double pitchPx = m_pitch * radius / qDegreesToRadians(45.0);

    // 空（青グラデーション）
    QLinearGradient skyGrad(0, -radius * 2, 0, pitchPx);
    skyGrad.setColorAt(0.0, QColor(0, 50, 120));
    skyGrad.setColorAt(1.0, QColor(70, 140, 220));
    p.fillRect(-radius * 2, -radius * 2, radius * 4, pitchPx + radius * 2, skyGrad);

    // 地面（茶色グラデーション）
    QLinearGradient gndGrad(0, pitchPx, 0, radius * 2);
    gndGrad.setColorAt(0.0, QColor(100, 70, 30));
    gndGrad.setColorAt(1.0, QColor(50, 35, 15));
    p.fillRect(-radius * 2, pitchPx, radius * 4, radius * 4, gndGrad);

    // 水平線
    p.setPen(QPen(Qt::white, 2));
    p.drawLine(-radius * 2, static_cast<int>(pitchPx),
               radius * 2, static_cast<int>(pitchPx));

    p.restore();
}

void AttitudeIndicator::drawPitchScale(QPainter &p, int cx, int cy, int radius)
{
    p.save();
    p.translate(cx, cy);
    p.rotate(qRadiansToDegrees(-m_roll));

    double scale = radius / qDegreesToRadians(45.0);

    p.setPen(QPen(Qt::white, 1.5));
    QFont f = p.font();
    f.setPixelSize(10);
    p.setFont(f);

    for (int deg = -40; deg <= 40; deg += 10) {
        if (deg == 0) continue;
        double y = m_pitch * scale - deg * qDegreesToRadians(1.0) * scale;
        int lineW = (deg % 20 == 0) ? 40 : 20;
        p.drawLine(-lineW, static_cast<int>(y), lineW, static_cast<int>(y));

        if (deg % 20 == 0) {
            QString text = QString::number(qAbs(deg));
            p.drawText(-lineW - 20, static_cast<int>(y) + 4, text);
            p.drawText(lineW + 5, static_cast<int>(y) + 4, text);
        }
    }

    p.restore();
}

void AttitudeIndicator::drawRollPointer(QPainter &p, int cx, int cy, int radius)
{
    p.save();
    p.translate(cx, cy);

    // ロールスケール
    p.setPen(QPen(Qt::white, 1.5));
    int markR = radius + 2;
    for (int deg : {-60, -45, -30, -20, -10, 0, 10, 20, 30, 45, 60}) {
        double angle = qDegreesToRadians(static_cast<double>(deg) - 90.0);
        int len = (deg % 30 == 0) ? 12 : 7;
        int x1 = static_cast<int>(markR * qCos(angle));
        int y1 = static_cast<int>(markR * qSin(angle));
        int x2 = static_cast<int>((markR + len) * qCos(angle));
        int y2 = static_cast<int>((markR + len) * qSin(angle));
        p.drawLine(x1, y1, x2, y2);
    }

    // ロール三角ポインタ
    p.rotate(qRadiansToDegrees(-m_roll));
    QPainterPath tri;
    tri.moveTo(0, -(radius - 2));
    tri.lineTo(-6, -(radius + 10));
    tri.lineTo(6, -(radius + 10));
    tri.closeSubpath();
    p.fillPath(tri, QColor(255, 200, 0));
    p.setPen(QPen(QColor(200, 160, 0), 1));
    p.drawPath(tri);

    p.restore();
}

void AttitudeIndicator::drawAircraftSymbol(QPainter &p, int cx, int cy)
{
    p.save();
    p.translate(cx, cy);

    // 中央ドット
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 200, 0));
    p.drawEllipse(QPoint(0, 0), 5, 5);

    // 翼
    p.setPen(QPen(QColor(255, 200, 0), 3));
    p.drawLine(-50, 0, -20, 0);
    p.drawLine(20, 0, 50, 0);
    p.drawLine(-50, 0, -50, 8);
    p.drawLine(50, 0, 50, 8);

    p.restore();
}

void AttitudeIndicator::drawHeadingBar(QPainter &p, int x, int y, int width)
{
    p.save();

    // 背景
    p.fillRect(x, y, width, 20, QColor(40, 40, 45));
    p.setPen(QPen(QColor(80, 80, 90), 1));
    p.drawRect(x, y, width, 20);

    double heading = qRadiansToDegrees(m_yaw);
    if (heading < 0) heading += 360.0;

    // スケール描画
    p.setPen(QPen(Qt::white, 1));
    QFont f = p.font();
    f.setPixelSize(10);
    p.setFont(f);

    int centerX = x + width / 2;
    double pxPerDeg = width / 120.0;

    p.setClipRect(x, y, width, 20);

    for (int deg = -180; deg <= 540; deg += 5) {
        double offset = (deg - heading) * pxPerDeg;
        int px = centerX + static_cast<int>(offset);

        if (px < x - 10 || px > x + width + 10) continue;

        if (deg % 30 == 0) {
            p.drawLine(px, y, px, y + 8);
            int displayDeg = deg % 360;
            if (displayDeg < 0) displayDeg += 360;
            QString label;
            switch (displayDeg) {
                case 0:   label = "N"; break;
                case 90:  label = "E"; break;
                case 180: label = "S"; break;
                case 270: label = "W"; break;
                default:  label = QString::number(displayDeg); break;
            }
            p.drawText(px - 10, y + 18, 20, 10, Qt::AlignCenter, label);
        } else if (deg % 10 == 0) {
            p.drawLine(px, y, px, y + 5);
        }
    }

    // 中央マーカー
    p.setPen(QPen(QColor(255, 200, 0), 2));
    p.drawLine(centerX, y, centerX, y + 10);

    p.setClipping(false);
    p.restore();
}

void AttitudeIndicator::drawBorder(QPainter &p, int cx, int cy, int radius)
{
    p.save();
    QPen pen(QColor(60, 60, 70), 3);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QPoint(cx, cy), radius, radius);

    // 外側のリング
    QPen outerPen(QColor(80, 80, 90), 1);
    p.setPen(outerPen);
    p.drawEllipse(QPoint(cx, cy), radius + 15, radius + 15);
    p.restore();
}
