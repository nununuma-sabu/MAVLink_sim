#include "MavlinkUdpLink.h"
#include <QDebug>

MavlinkUdpLink::MavlinkUdpLink(QObject *parent)
    : QObject(parent)
    , m_socket(new QUdpSocket(this))
{
    connect(m_socket, &QUdpSocket::readyRead, this, &MavlinkUdpLink::readPendingDatagrams);
}

MavlinkUdpLink::~MavlinkUdpLink()
{
    stop();
}

bool MavlinkUdpLink::start(uint16_t localPort, const QHostAddress &remoteHost, uint16_t remotePort)
{
    m_remoteHost = remoteHost;
    m_remotePort = remotePort;

    if (!m_socket->bind(QHostAddress::AnyIPv4, localPort, QAbstractSocket::ShareAddress | QAbstractSocket::ReuseAddressHint)) {
        qWarning() << "[MavlinkUdpLink] バインド失敗 ポート:" << localPort
                    << "エラー:" << m_socket->errorString();
        return false;
    }

    m_connected = true;
    m_hasRemote = true;
    emit connectionChanged(true);
    qDebug() << "[MavlinkUdpLink] 待ち受け開始 ポート:" << localPort
             << "送信先:" << remoteHost.toString() << ":" << remotePort;
    return true;
}

void MavlinkUdpLink::stop()
{
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->close();
    }
    m_connected = false;
    emit connectionChanged(false);
    qDebug() << "[MavlinkUdpLink] 停止";
}

void MavlinkUdpLink::sendMessage(const mavlink_message_t &msg)
{
    if (!m_hasRemote) return;

    uint8_t buffer[MAVLINK_MAX_PACKET_LEN];
    int len = mavlink_msg_to_send_buffer(buffer, &msg);

    qint64 sent = m_socket->writeDatagram(
        reinterpret_cast<const char*>(buffer), len,
        m_remoteHost, m_remotePort);

    if (sent < 0) {
        qWarning() << "[MavlinkUdpLink] 送信失敗:" << m_socket->errorString();
    }
}

void MavlinkUdpLink::setRemoteAddress(const QHostAddress &host, uint16_t port)
{
    m_remoteHost = host;
    m_remotePort = port;
    m_hasRemote = true;
}

void MavlinkUdpLink::readPendingDatagrams()
{
    while (m_socket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(static_cast<int>(m_socket->pendingDatagramSize()));
        QHostAddress sender;
        uint16_t senderPort;

        m_socket->readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);

        // GCSのアドレスを自動学習
        if (sender != m_remoteHost || senderPort != m_remotePort) {
            m_remoteHost = sender;
            m_remotePort = senderPort;
            m_hasRemote = true;
            qDebug() << "[MavlinkUdpLink] GCS検出:" << sender.toString() << ":" << senderPort;
        }

        // MAVLinkパース
        mavlink_message_t msg;
        for (int i = 0; i < datagram.size(); ++i) {
            uint8_t byte = static_cast<uint8_t>(datagram.at(i));
            if (mavlink_parse_char(m_parseChan, byte, &msg, &m_parseStatus)) {
                emit messageReceived(msg);
            }
        }
    }
}
