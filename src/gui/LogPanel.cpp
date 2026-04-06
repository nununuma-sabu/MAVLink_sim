#include "LogPanel.h"
#include <QHBoxLayout>
#include <QScrollBar>

LogPanel::LogPanel(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
}

void LogPanel::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    // ヘッダーバー
    auto *header = new QWidget();
    header->setStyleSheet("background: #252530; border-bottom: 1px solid #3a3a42;");
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(8, 2, 8, 2);

    auto *lblTitle = new QLabel("📡 MAVLink ログ");
    lblTitle->setStyleSheet("color: #3498db; font-weight: bold; font-size: 12px;");
    headerLayout->addWidget(lblTitle);

    m_lblCount = new QLabel("0 行");
    m_lblCount->setStyleSheet("color: #666; font-size: 11px;");
    headerLayout->addWidget(m_lblCount);

    headerLayout->addStretch();

    auto *btnClear = new QPushButton("クリア");
    btnClear->setStyleSheet(
        "QPushButton { background: #3a3a45; color: #aaa; border: 1px solid #555; "
        "border-radius: 3px; padding: 2px 10px; font-size: 11px; }"
        "QPushButton:hover { background: #4a4a55; color: #fff; }");
    connect(btnClear, &QPushButton::clicked, this, &LogPanel::clearLog);
    headerLayout->addWidget(btnClear);

    layout->addWidget(header);

    // ログ表示エリア
    m_logView = new QTextEdit();
    m_logView->setReadOnly(true);
    m_logView->setStyleSheet(
        "QTextEdit { background: #1a1a20; color: #0f0; "
        "font-family: 'Courier New', monospace; font-size: 11px; "
        "border: none; padding: 4px; }"
        "QScrollBar:vertical { background: #1a1a20; width: 6px; }"
        "QScrollBar::handle:vertical { background: #444; border-radius: 3px; min-height: 20px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }");
    layout->addWidget(m_logView);
}

void LogPanel::appendLog(const QString &message)
{
    // タイムスタンプ付き
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");

    // メッセージの種類に応じて色を変える
    QString color;
    if (message.startsWith("→")) {
        color = "#2ecc71";  // 送信: 緑
    } else if (message.startsWith("←")) {
        color = "#3498db";  // 受信: 青
    } else if (message.contains("エラー") || message.contains("失敗")) {
        color = "#e74c3c";  // エラー: 赤
    } else {
        color = "#aaa";     // その他: グレー
    }

    QString html = QString("<span style='color:#555;'>%1</span> "
                           "<span style='color:%2;'>%3</span>")
                   .arg(timestamp, color, message.toHtmlEscaped());

    m_logView->append(html);
    m_lineCount++;

    // 行数制限
    if (m_lineCount > MAX_LINES) {
        QTextCursor cursor = m_logView->textCursor();
        cursor.movePosition(QTextCursor::Start);
        cursor.movePosition(QTextCursor::Down, QTextCursor::KeepAnchor, m_lineCount - MAX_LINES);
        cursor.removeSelectedText();
        m_lineCount = MAX_LINES;
    }

    // 自動スクロール
    QScrollBar *sb = m_logView->verticalScrollBar();
    sb->setValue(sb->maximum());

    m_lblCount->setText(QString::number(m_lineCount) + " 行");
}

void LogPanel::clearLog()
{
    m_logView->clear();
    m_lineCount = 0;
    m_lblCount->setText("0 行");
}
