#pragma once
#include <QObject>
#include <QByteArray>
#include <QString>
#include <QTcpSocket>

// A tiny WebSocket client that listens to ComfyUI's /ws channel and reports
// ONLY the KSampler's step progress (no extra Qt modules needed).
class ProgressSocket : public QObject {
    Q_OBJECT
public:
    explicit ProgressSocket(QObject* parent = nullptr);

    void open(const QString& host, int port, const QString& clientId,
              const QString& samplerNodeId);
    void close();

signals:
    void samplerProgress(int value, int max);
    void executionError(const QString& message);

private:
    void onConnected();
    void onReadyRead();
    void parseFrames();
    void handleText(const QByteArray& json);
    void sendPong(const QByteArray& payload);

    QTcpSocket m_sock;
    QByteArray m_buf;          // bytes received but not parsed yet
    QByteArray m_msg;          // text message being assembled from fragments
    int m_fragKind = 0;        // 1 = text fragments, 2 = binary fragments (ignored)
    bool m_upgraded = false;   // true after the "101 Switching Protocols" answer

    QString m_host;
    int m_port = 0;
    QString m_clientId;
    QString m_samplerNode;     // node id of the KSampler, e.g. "19"
    QString m_currentNode;     // node ComfyUI says it is executing right now
};
