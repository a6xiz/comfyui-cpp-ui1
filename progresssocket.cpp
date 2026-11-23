#include "progresssocket.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>

ProgressSocket::ProgressSocket(QObject* parent) : QObject(parent) {
    connect(&m_sock, &QTcpSocket::connected, this, &ProgressSocket::onConnected);
    connect(&m_sock, &QTcpSocket::readyRead, this, &ProgressSocket::onReadyRead);
}

void ProgressSocket::open(const QString& host, int port, const QString& clientId,
                          const QString& samplerNodeId)
{
    close();
    m_host = host;
    m_port = port;
    m_clientId = clientId;
    m_samplerNode = samplerNodeId;
    m_currentNode.clear();
    m_buf.clear();
    m_msg.clear();
    m_fragKind = 0;
    m_upgraded = false;
    m_sock.connectToHost(host, quint16(port));
}

void ProgressSocket::close() {
    m_sock.abort();
    m_buf.clear();
    m_msg.clear();
    m_upgraded = false;
}

void ProgressSocket::onConnected() {
    QByteArray key;
    for (int i = 0; i < 16; ++i)
        key.append(char(QRandomGenerator::global()->bounded(256)));

    QByteArray req;
    req += "GET /ws?clientId=" + m_clientId.toUtf8() + " HTTP/1.1\r\n";
    req += "Host: " + m_host.toUtf8() + ":" + QByteArray::number(m_port) + "\r\n";
    req += "Upgrade: websocket\r\n";
    req += "Connection: Upgrade\r\n";
    req += "Sec-WebSocket-Key: " + key.toBase64() + "\r\n";
    req += "Sec-WebSocket-Version: 13\r\n\r\n";
    m_sock.write(req);
}

void ProgressSocket::onReadyRead() {
    m_buf += m_sock.readAll();

    if (!m_upgraded) {
        int end = m_buf.indexOf("\r\n\r\n");
        if (end < 0) return;                       // answer not complete yet
        QByteArray head = m_buf.left(end);
        m_buf.remove(0, end + 4);
        if (!head.startsWith("HTTP/1.1 101")) {    // server refused the websocket
            close();
            return;
        }
        m_upgraded = true;
    }
    parseFrames();
}

// reads as many complete websocket frames as we have
void ProgressSocket::parseFrames() {
    while (true) {
        if (m_buf.size() < 2) return;

        const uchar b0 = uchar(m_buf[0]);
        const uchar b1 = uchar(m_buf[1]);
        const bool fin = b0 & 0x80;
        const int opcode = b0 & 0x0F;
        const bool masked = b1 & 0x80;
        quint64 len = b1 & 0x7F;
        int pos = 2;

        if (len == 126) {
            if (m_buf.size() < 4) return;
            len = (quint64(uchar(m_buf[2])) << 8) | uchar(m_buf[3]);
            pos = 4;
        } else if (len == 127) {
            if (m_buf.size() < 10) return;
            len = 0;
            for (int i = 0; i < 8; ++i)
                len = (len << 8) | uchar(m_buf[2 + i]);
            pos = 10;
        }

        QByteArray mask;
        if (masked) {
            if (m_buf.size() < pos + 4) return;
            mask = m_buf.mid(pos, 4);
            pos += 4;
        }

        if (len > 64ull * 1024 * 1024) { close(); return; }     // nonsense size
        if (quint64(m_buf.size()) < quint64(pos) + len) return; // frame not complete yet

        QByteArray payload = m_buf.mid(pos, int(len));
        m_buf.remove(0, pos + int(len));
        if (masked)
            for (int i = 0; i < payload.size(); ++i)
                payload[i] = char(payload[i] ^ mask[i % 4]);

        switch (opcode) {
        case 0x1:   // text
            if (fin) { handleText(payload); }
            else     { m_msg = payload; m_fragKind = 1; }
            break;
        case 0x0:   // continuation
            if (m_fragKind == 1) {
                m_msg += payload;
                if (fin) { handleText(m_msg); m_msg.clear(); m_fragKind = 0; }
            } else if (fin) {
                m_fragKind = 0;
            }
            break;
        case 0x2:   // binary = preview pictures, we do not need them
            m_fragKind = fin ? 0 : 2;
            break;
        case 0x8:   // close
            close();
            return;
        case 0x9:   // ping -> answer with pong
            sendPong(payload);
            break;
        default:    // 0xA pong etc.
            break;
        }
    }
}

void ProgressSocket::handleText(const QByteArray& json) {
    QJsonObject root = QJsonDocument::fromJson(json).object();
    const QString type = root.value("type").toString();
    const QJsonObject data = root.value("data").toObject();

    if (type == "executing") {
        // the node ComfyUI is working on now (null when the job is finished)
        m_currentNode = data.value("node").toVariant().toString();
    }
    else if (type == "progress") {
        QString node = data.contains("node") ? data.value("node").toVariant().toString()
                                             : m_currentNode;
        if (node == m_samplerNode)
            emit samplerProgress(data.value("value").toInt(), data.value("max").toInt());
    }
    else if (type == "progress_state") {
        // newer ComfyUI: one message with the state of every node
        QJsonObject nodes = data.value("nodes").toObject();
        if (nodes.contains(m_samplerNode)) {
            QJsonObject n = nodes.value(m_samplerNode).toObject();
            emit samplerProgress(n.value("value").toInt(), n.value("max").toInt());
        }
    }
    else if (type == "execution_error") {
        QString msg = data.value("exception_message").toString().trimmed();
        QString nodeType = data.value("node_type").toString();
        if (!nodeType.isEmpty()) msg = nodeType + ": " + msg;
        emit executionError(msg);
    }
}

// client frames must be masked
void ProgressSocket::sendPong(const QByteArray& payload) {
    const int n = qMin(int(payload.size()), 125);
    QByteArray frame;
    frame.append(char(0x8A));
    frame.append(char(0x80 | n));
    char mask[4];
    for (int i = 0; i < 4; ++i) {
        mask[i] = char(QRandomGenerator::global()->bounded(256));
        frame.append(mask[i]);
    }
    for (int i = 0; i < n; ++i)
        frame.append(char(payload[i] ^ mask[i % 4]));
    m_sock.write(frame);
}
