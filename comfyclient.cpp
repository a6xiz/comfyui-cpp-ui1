#define _WIN32_WINNT 0x0A00
#include "json.hpp"      // keep these two FIRST, before the Qt includes
#include "httplib.h"
#include "workflowbuilder.h"
#include "comfyclient.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUuid>
#ifdef Q_OS_ANDROID
#include <QJniEnvironment>
#include <QJniObject>
#endif

#include <chrono>
#include <random>
#include <string>
#include <thread>

using json = nlohmann::json;

// ------------------------------------------------------------------
//  small helpers
// ------------------------------------------------------------------

static long long generate_seed() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<long long> dis(1, 999999999999999LL);
    return dis(gen);
}

// the workflow json is packed inside the app (see CMakeLists.txt)
static bool loadWorkflow(json& out, QString& error) {
    QFile f(":/qt/qml/comfyui_cpp_ui/ComfyUI_00994_2.json");
    if (!f.open(QIODevice::ReadOnly)) {
        error = "Could not open the workflow json inside the app";
        return false;
    }
    QByteArray data = f.readAll();
    out = json::parse(data.constData(), data.constData() + data.size(), nullptr, false);
    if (out.is_discarded() || !out.is_object()) {
        error = "The workflow json is not valid";
        return false;
    }
    return true;
}

// finds the node that loads the model (checkpoint / unet)
struct ModelNode {
    std::string id;
    std::string classType;
    std::string key;
};

static bool findModelNode(const json& wf, ModelNode& out) {
    for (auto it = wf.begin(); it != wf.end(); ++it) {
        const json& node = it.value();
        if (!node.is_object() || !node.contains("inputs") || !node.contains("class_type"))
            continue;
        if (!node["inputs"].is_object() || !node["class_type"].is_string())
            continue;
        for (const char* key : {"ckpt_name", "unet_name"}) {
            if (node["inputs"].contains(key)) {
                out.id = it.key();
                out.classType = node["class_type"].get<std::string>();
                out.key = key;
                return true;
            }
        }
    }
    return false;
}

// ComfyUI describes a dropdown as  [ ["a","b"], {...} ]  or  ["COMBO", {"options":[...]}]
static QStringList comboOptions(const json& spec) {
    QStringList list;
    if (!spec.is_array() || spec.empty())
        return list;
    const json& first = spec[0];
    if (first.is_array()) {
        for (const auto& v : first)
            if (v.is_string())
                list << QString::fromStdString(v.get<std::string>());
    } else if (first.is_string() && spec.size() > 1 && spec[1].is_object()
               && spec[1].contains("options") && spec[1]["options"].is_array()) {
        for (const auto& v : spec[1]["options"])
            if (v.is_string())
                list << QString::fromStdString(v.get<std::string>());
    }
    return list;
}

struct ImageRef {
    std::string filename, subfolder, type;
    bool found = false;
};

// looks through the "outputs" of a finished job for the picture
static ImageRef findOutputImage(const json& outputs) {
    ImageRef best;
    if (!outputs.is_object())
        return best;
    for (auto it = outputs.begin(); it != outputs.end(); ++it) {
        const json& node = it.value();
        if (!node.is_object() || !node.contains("images") || !node["images"].is_array())
            continue;
        for (const auto& im : node["images"]) {
            if (!im.is_object() || !im.contains("filename"))
                continue;
            ImageRef r;
            r.filename = im.value("filename", std::string());
            r.subfolder = im.value("subfolder", std::string());
            r.type = im.value("type", std::string("output"));
            r.found = true;
            // prefer real "output" images over "temp" previews
            if (!best.found || r.type == "output")
                best = r;
        }
    }
    return best;
}


#ifdef Q_OS_ANDROID
// Saves a picture into  Pictures/ComfyClient  using Android's MediaStore.
// Works on Android 10+ and needs no storage permission.
static bool saveToGallery(const QByteArray& bytes, const QString& fileName, const QString& mime)
{
    if (QNativeInterface::QAndroidApplication::sdkVersion() < 29)
        return false;

    QJniEnvironment env;
    QJniObject context = QNativeInterface::QAndroidApplication::context();
    QJniObject resolver = context.callObjectMethod(
        "getContentResolver", "()Landroid/content/ContentResolver;");
    if (!resolver.isValid()) return false;

    auto putString = [](QJniObject& cv, const char* key, const QString& value) {
        cv.callMethod<void>("put", "(Ljava/lang/String;Ljava/lang/String;)V",
                            QJniObject::fromString(QString::fromLatin1(key)).object<jstring>(),
                            QJniObject::fromString(value).object<jstring>());
    };
    auto putInt = [](QJniObject& cv, const char* key, int value) {
        QJniObject boxed = QJniObject::callStaticObjectMethod(
            "java/lang/Integer", "valueOf", "(I)Ljava/lang/Integer;", jint(value));
        cv.callMethod<void>("put", "(Ljava/lang/String;Ljava/lang/Integer;)V",
                            QJniObject::fromString(QString::fromLatin1(key)).object<jstring>(),
                            boxed.object());
    };

    QJniObject values("android/content/ContentValues");
    putString(values, "_display_name", fileName);
    putString(values, "mime_type", mime);
    putString(values, "relative_path", "Pictures/ComfyClient");
    putInt(values, "is_pending", 1);

    QJniObject collection = QJniObject::callStaticObjectMethod(
        "android/provider/MediaStore$Images$Media", "getContentUri",
        "(Ljava/lang/String;)Landroid/net/Uri;",
        QJniObject::fromString("external_primary").object<jstring>());

    QJniObject uri = resolver.callObjectMethod(
        "insert", "(Landroid/net/Uri;Landroid/content/ContentValues;)Landroid/net/Uri;",
        collection.object(), values.object());
    if (env.checkAndClearExceptions() || !uri.isValid()) return false;

    QJniObject os = resolver.callObjectMethod(
        "openOutputStream", "(Landroid/net/Uri;)Ljava/io/OutputStream;", uri.object());
    if (env.checkAndClearExceptions() || !os.isValid()) return false;

    jbyteArray arr = env->NewByteArray(jsize(bytes.size()));
    env->SetByteArrayRegion(arr, 0, jsize(bytes.size()),
                            reinterpret_cast<const jbyte*>(bytes.constData()));
    os.callMethod<void>("write", "([B)V", arr);
    os.callMethod<void>("close");
    env->DeleteLocalRef(arr);
    if (env.checkAndClearExceptions()) return false;

    // picture is complete -> make it visible in the gallery
    QJniObject done("android/content/ContentValues");
    putInt(done, "is_pending", 0);
    resolver.callMethod<jint>(
        "update", "(Landroid/net/Uri;Landroid/content/ContentValues;Ljava/lang/String;[Ljava/lang/String;)I",
        uri.object(), done.object(), nullptr, nullptr);
    env.checkAndClearExceptions();
    return true;
}
#endif

static void setupClient(httplib::Client& cli) {
    cli.set_connection_timeout(3);
    cli.set_read_timeout(30);
    cli.set_write_timeout(30);
}

// ------------------------------------------------------------------
//  ComfyClient
// ------------------------------------------------------------------

ComfyClient::ComfyClient(QObject* parent) : QObject(parent) {
    m_ws = new ProgressSocket(this);
    connect(m_ws, &ProgressSocket::samplerProgress, this, &ComfyClient::samplerProgress);
    connect(m_ws, &ProgressSocket::executionError, this, [this](const QString& msg) {
        std::lock_guard<std::mutex> lock(m_errMutex);
        m_lastError = msg;
    });
}

void ComfyClient::setAutoSave(bool on) { m_autoSave = on; }

ComfyClient::~ComfyClient() {
    m_stop = true;
    if (m_modelsThread.joinable()) m_modelsThread.join();
    if (m_genThread.joinable()) m_genThread.join();
}

void ComfyClient::setServer(const QString& address) {
    QString a = address.trimmed();
    if (a.startsWith("http://"))  a = a.mid(7);
    if (a.startsWith("https://")) a = a.mid(8);
    while (a.endsWith('/')) a.chop(1);

    QString host = a;
    int port = 8188;
    int colon = a.lastIndexOf(':');
    if (colon > 0) {
        bool ok = false;
        int p = a.mid(colon + 1).toInt(&ok);
        if (ok && p > 0 && p < 65536) {
            port = p;
            host = a.left(colon);
        }
    }
    if (host.isEmpty()) host = "127.0.0.1";
    m_server = Server{host.toStdString(), port};
}

// ---------------- models ----------------

void ComfyClient::refreshModels() {
    if (m_modelsBusy) return;
    if (m_modelsThread.joinable()) m_modelsThread.join();
    m_modelsBusy = true;
    Server s = m_server;
    m_modelsThread = std::thread([this, s]() {
        runModels(s);
        m_modelsBusy = false;
    });
}

void ComfyClient::runModels(Server s) {
    try {
        json wf;
        QString err;
        if (!loadWorkflow(wf, err)) { emit modelsFailed(err); return; }

        ModelNode mn;
        if (!findModelNode(wf, mn)) {
            emit modelsFailed("No model loader node found in the workflow");
            return;
        }

        QString current;
        const json& cur = wf[mn.id]["inputs"][mn.key];
        if (cur.is_string()) current = QString::fromStdString(cur.get<std::string>());

        httplib::Client cli(s.host, s.port);
        setupClient(cli);
        auto res = cli.Get("/object_info/" + mn.classType);
        if (!res) {
            emit modelsFailed("Connection failed (is ComfyUI running?)");
            return;
        }
        if (res->status != 200) {
            emit modelsFailed(QString("ComfyUI answered HTTP %1").arg(res->status));
            return;
        }

        json info = json::parse(res->body, nullptr, false);
        QStringList list;
        if (!info.is_discarded() && info.contains(mn.classType)) {
            const json& node = info[mn.classType];
            if (node.contains("input") && node["input"].contains("required")
                && node["input"]["required"].contains(mn.key)) {
                list = comboOptions(node["input"]["required"][mn.key]);
            }
        }
        if (list.isEmpty()) {
            emit modelsFailed("ComfyUI returned no models");
            return;
        }
        emit modelsReady(list, current);
    } catch (const std::exception& e) {
        emit modelsFailed(QString("Exception: ") + QString::fromUtf8(e.what()));
    }
}

// ---------------- picking an image ----------------

QString ComfyClient::importImage(const QUrl& url) {
    QString src = url.isLocalFile() ? url.toLocalFile() : url.toString();
    QFile in(src);                       // QFile can also open Android content:// urls
    if (!in.open(QIODevice::ReadOnly)) {
        qDebug() << "importImage: cannot open" << src;
        return QString();
    }

    QString ext = QFileInfo(url.path()).suffix().toLower();
    if (ext.isEmpty() || ext.size() > 5) ext = "png";

    QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/uploads";
    QDir().mkpath(dir);
    QString path = dir + "/upload_" + QString::number(QDateTime::currentMSecsSinceEpoch())
                   + "." + ext;

    QFile out(path);
    if (!out.open(QIODevice::WriteOnly)) return QString();
    out.write(in.readAll());
    out.close();

    // delete older uploads
    const auto old = QDir(dir).entryInfoList(QStringList() << "upload_*", QDir::Files);
    for (const QFileInfo& fi : old)
        if (fi.absoluteFilePath() != path) QFile::remove(fi.absoluteFilePath());

    return QUrl::fromLocalFile(path).toString();
}

// ---------------- generating ----------------

void ComfyClient::generate(const QString& positive, const QString& negative,
                           const QString& imageUrl, qlonglong seed,
                           int steps, double cfg, int width, int height,
                           const QString& model)
{
    qDebug() << "generate() called";
    if (m_genBusy) {
        emit failed("Already generating, please wait");
        return;
    }
    if (m_genThread.joinable()) m_genThread.join();
    m_genBusy = true;
    Server s = m_server;

    {
        std::lock_guard<std::mutex> lock(m_errMutex);
        m_lastError.clear();
    }
    // listen for the KSampler progress (this runs in the GUI thread)
    const QString clientId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_ws->open(QString::fromStdString(s.host), s.port, clientId,
               QString::fromLatin1(wfb::kSampler));

    m_genThread = std::thread([=]() {
        runGenerate(s, positive, negative, imageUrl, seed, steps, cfg, width, height,
                    model, clientId);
        m_genBusy = false;
        QMetaObject::invokeMethod(this, [this]() { m_ws->close(); }, Qt::QueuedConnection);
    });
}

void ComfyClient::runGenerate(Server s, QString positive, QString negative, QString imageUrl,
                              qlonglong seed, int steps, double cfg, int width, int height,
                              QString model, QString clientId)
{
    try {
        json workflow;
        QString err;
        if (!loadWorkflow(workflow, err)) { emit failed(err); return; }

        if (seed == -1) seed = generate_seed();
        wfb::applyNumbers(workflow, negative.toStdString(), (long long)seed,
                          steps, cfg, width, height);

        ModelNode mn;
        if (!model.isEmpty() && findModelNode(workflow, mn))
            workflow[mn.id]["inputs"][mn.key] = model.toStdString();

        httplib::Client cli(s.host, s.port);
        setupClient(cli);

        if (!imageUrl.isEmpty()) {
            // ---- image-to-image: upload the picked picture first ----
            emit progress("Uploading image...");
            QString localPath = QUrl(imageUrl).toLocalFile();
            QFile f(localPath);
            if (!f.open(QIODevice::ReadOnly)) {
                emit failed("Could not read the picked image");
                return;
            }
            QByteArray bytes = f.readAll();
            std::string data(bytes.constData(), (size_t)bytes.size());
            std::string filename = QFileInfo(localPath).fileName().toStdString();

            httplib::UploadFormDataItems items = {
                { "image", data, filename, "application/octet-stream" }
            };
            auto up = cli.Post("/upload/image", items);
            if (!up) { emit failed("Connection failed during upload (is ComfyUI running?)"); return; }
            if (up->status != 200) {
                emit failed(QString("Upload failed: HTTP %1").arg(up->status));
                return;
            }
            json upJson = json::parse(up->body, nullptr, false);
            if (upJson.is_discarded() || !upJson.contains("name")) {
                emit failed("Upload answer was not understood");
                return;
            }
            wfb::applyWithImage(workflow, positive.toStdString(),
                                upJson["name"].get<std::string>());
        } else {
            // ---- text-to-image: only your text, image part removed ----
            wfb::applyTextOnly(workflow, positive.toStdString());
        }

        // ---- queue it ----
        emit progress("Sending to ComfyUI...");
        json payload;
        payload["prompt"] = workflow;
        payload["client_id"] = clientId.toStdString();
        auto res = cli.Post("/prompt", payload.dump(), "application/json");
        if (!res) {
            emit failed("Connection failed (is ComfyUI running, and is the address right?)");
            return;
        }
        if (res->status != 200) {
            emit failed(QString("HTTP %1: %2").arg(res->status)
                            .arg(QString::fromStdString(res->body)));
            return;
        }
        json queuedJson = json::parse(res->body, nullptr, false);
        if (queuedJson.is_discarded() || !queuedJson.contains("prompt_id")) {
            emit failed("ComfyUI answer had no prompt_id");
            return;
        }
        std::string promptId = queuedJson["prompt_id"].get<std::string>();
        qDebug() << "queued, id:" << QString::fromStdString(promptId);

        // ---- wait for the result ----
        auto start = std::chrono::steady_clock::now();
        ImageRef img;
        while (!m_stop) {
            std::this_thread::sleep_for(std::chrono::milliseconds(800));
            long long secs = std::chrono::duration_cast<std::chrono::seconds>(
                                 std::chrono::steady_clock::now() - start).count();
            if (secs > 900) {
                emit failed("Timed out waiting for the image");
                return;
            }
            emit progress(QString("Generating... %1s").arg(secs));

            auto h = cli.Get("/history/" + promptId);
            if (!h || h->status != 200) continue;
            json hist = json::parse(h->body, nullptr, false);
            if (hist.is_discarded() || !hist.contains(promptId)) continue;

            const json& entry = hist[promptId];
            if (entry.contains("outputs")) {
                img = findOutputImage(entry["outputs"]);
                if (img.found) break;
            }
            if (entry.contains("status") && entry["status"].is_object()) {
                const json& st = entry["status"];
                if (st.value("status_str", std::string()) == "error") {
                    QString detail;
                    {
                        std::lock_guard<std::mutex> lock(m_errMutex);
                        detail = m_lastError;
                    }
                    emit failed(detail.isEmpty()
                        ? QString("ComfyUI reported an error while running the workflow")
                        : detail);
                    return;
                }
                if (st.value("completed", false)) {
                    emit failed("The job finished but no image was found in its outputs");
                    return;
                }
            }
        }
        if (!img.found) return;     // app is closing

        // ---- download the picture ----
        emit progress("Downloading image...");
        httplib::Params params = {
            {"filename", img.filename},
            {"subfolder", img.subfolder},
            {"type", img.type}
        };
        auto pic = cli.Get("/view", params, httplib::Headers());
        if (!pic || pic->status != 200) {
            emit failed("Could not download the finished image");
            return;
        }

        QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        QDir().mkpath(dir);
        QString ext = QFileInfo(QString::fromStdString(img.filename)).suffix();
        if (ext.isEmpty()) ext = "png";
        QString path = dir + "/result_" + QString::number(QDateTime::currentMSecsSinceEpoch())
                       + "." + ext;
        QFile out(path);
        if (!out.open(QIODevice::WriteOnly)) {
            emit failed("Could not save the image on this device");
            return;
        }
        out.write(pic->body.data(), (qint64)pic->body.size());
        out.close();

        // remove older results
        const auto old = QDir(dir).entryInfoList(QStringList() << "result_*", QDir::Files);
        for (const QFileInfo& fi : old)
            if (fi.absoluteFilePath() != path) QFile::remove(fi.absoluteFilePath());

        emit imageReady(QUrl::fromLocalFile(path).toString());

#ifdef Q_OS_ANDROID
        if (m_autoSave) {
            QString mime = "image/png";
            const QString e = ext.toLower();
            if (e == "jpg" || e == "jpeg") mime = "image/jpeg";
            else if (e == "webp") mime = "image/webp";
            QString name = "ComfyUI_"
                           + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss")
                           + "." + ext;
            QByteArray bytes(pic->body.data(), int(pic->body.size()));
            bool ok = saveToGallery(bytes, name, mime);
            emit savedToGallery(ok, "Pictures/ComfyClient");
        }
#endif
    } catch (const std::exception& e) {
        qDebug() << "exception:" << e.what();
        emit failed(QString("Exception: ") + QString::fromUtf8(e.what()));
    }
}
