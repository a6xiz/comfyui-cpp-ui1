#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include "progresssocket.h"

class ComfyClient : public QObject {
    Q_OBJECT
public:
    explicit ComfyClient(QObject* parent = nullptr);
    ~ComfyClient() override;

    // "192.168.1.10:8188" or "127.0.0.1:8188"
    Q_INVOKABLE void setServer(const QString& address);

    // asks ComfyUI which models it has (result arrives as modelsReady)
    Q_INVOKABLE void refreshModels();

    // sends the workflow, waits for the image (result arrives as imageReady)
    Q_INVOKABLE void generate(const QString& positive, const QString& negative,
                              const QString& imageUrl, qlonglong seed,
                              int steps, double cfg, int width, int height,
                              const QString& model);

    // copies a picked image (file:// or Android content://) into the app's own
    // folder and returns a normal file:// url, or "" if it could not be read
    Q_INVOKABLE QString importImage(const QUrl& url);

    // Android: save finished images into Pictures/ComfyClient
    Q_INVOKABLE void setAutoSave(bool on);

signals:
    void modelsReady(const QStringList& models, const QString& current);
    void modelsFailed(const QString& message);
    void progress(const QString& text);
    void imageReady(const QString& fileUrl);
    void failed(const QString& message);
    void samplerProgress(int value, int max);              // KSampler step progress only
    void savedToGallery(bool ok, const QString& where);    // Android auto-save result

private:
    struct Server {
        std::string host;
        int port;
    };

    void runModels(Server s);
    void runGenerate(Server s, QString positive, QString negative, QString imageUrl,
                     qlonglong seed, int steps, double cfg, int width, int height,
                     QString model, QString clientId);

    ProgressSocket* m_ws = nullptr;
    std::mutex m_errMutex;
    QString m_lastError;           // error text sent by ComfyUI over the websocket
    std::atomic<bool> m_autoSave{true};

    Server m_server{"127.0.0.1", 8188};

    std::thread m_modelsThread;
    std::thread m_genThread;
    std::atomic<bool> m_modelsBusy{false};
    std::atomic<bool> m_genBusy{false};
    std::atomic<bool> m_stop{false};
};
