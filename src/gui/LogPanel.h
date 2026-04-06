#ifndef LOGPANEL_H
#define LOGPANEL_H

#include <QWidget>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QDateTime>

/**
 * @brief MAVLink通信ログパネル
 *
 * 送受信メッセージのログをリアルタイム表示する。
 * 自動スクロール、行数制限、クリア機能付き。
 */
class LogPanel : public QWidget
{
    Q_OBJECT

public:
    explicit LogPanel(QWidget *parent = nullptr);

public slots:
    /// ログメッセージを追加
    void appendLog(const QString &message);
    /// ログをクリア
    void clearLog();

private:
    void setupUi();

    QTextEdit *m_logView;
    QLabel *m_lblCount;
    int m_lineCount = 0;
    static constexpr int MAX_LINES = 500;
};

#endif // LOGPANEL_H
