#ifndef MAVLINKUDPLINK_H
#define MAVLINKUDPLINK_H

#include <QObject>
#include <QUdpSocket>
#include <QHostAddress>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
#include <common/mavlink.h>
#pragma GCC diagnostic pop

/**
 * @brief MAVLink UDP通信レイヤー
 *
 * QUdpSocketを使用してMAVLinkメッセージを送受信する。
 * 受信データはmavlink_parse_char()でパースし、完全なメッセージを通知。
 */
class MavlinkUdpLink : public QObject
{
    Q_OBJECT

public:
    explicit MavlinkUdpLink(QObject *parent = nullptr);
    ~MavlinkUdpLink() override;

    /// UDP接続を開始
    /// @param localPort 受信ポート (デフォルト: 14540)
    /// @param remoteHost GCSのアドレス
    /// @param remotePort GCSのポート (デフォルト: 14550)
    bool start(uint16_t localPort = 14540,
               const QHostAddress &remoteHost = QHostAddress::LocalHost,
               uint16_t remotePort = 14550);

    /// 接続を停止
    void stop();

    /// MAVLinkメッセージを送信
    void sendMessage(const mavlink_message_t &msg);

    /// 接続状態
    bool isConnected() const { return m_connected; }

    /// リモートアドレスを設定（GCSからメッセージ受信時に自動更新）
    void setRemoteAddress(const QHostAddress &host, uint16_t port);

signals:
    /// 完全なMAVLinkメッセージを受信
    void messageReceived(const mavlink_message_t &msg);
    /// 接続状態変更
    void connectionChanged(bool connected);

private slots:
    void readPendingDatagrams();

private:
    QUdpSocket *m_socket = nullptr;
    QHostAddress m_remoteHost;
    uint16_t m_remotePort = 14550;
    bool m_connected = false;
    bool m_hasRemote = false;

    mavlink_status_t m_parseStatus = {};
    uint8_t m_parseChan = MAVLINK_COMM_0;
};

#endif // MAVLINKUDPLINK_H
