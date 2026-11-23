#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include "comfyclient.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    // needed so the app can remember its settings (server, blur, model)
    QCoreApplication::setOrganizationName("comfyui-cpp-ui");
    QCoreApplication::setApplicationName("ComfyUI Client");

    QQmlApplicationEngine engine;

    // make our C++ class visible to the screen under the name "comfy"
    ComfyClient client;
    engine.rootContext()->setContextProperty("comfy", &client);

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    engine.loadFromModule("comfyui_cpp_ui", "Main");

    return app.exec();
}
