#include <QApplication>
#include <QIcon>
#include <QLocalSocket>

#include "mainwindow.h"

static const char kLocalServerName[] = "UpdateServerInstance";

int main(int argc, char *argv[])
{
    // ---- 解析命令行参数 ----
    bool headless = false;
    quint16 port = 8080;

    for (int i = 1; i < argc; ++i) {
        QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == "--headless" || arg == "-H") {
            headless = true;
        } else if ((arg == "--port" || arg == "-p") && i + 1 < argc) {
            port = QString::fromLocal8Bit(argv[++i]).toUShort();
        }
    }

    QApplication app(argc, argv);
    app.setApplicationName("UpdateServer");
    app.setApplicationVersion(QStringLiteral(APP_VERSION));
    app.setWindowIcon(QIcon(QStringLiteral(":/resources/server.png")));

    // ---- 单实例检测 ----
    {
        QLocalSocket socket;
        socket.connectToServer(QLatin1String(kLocalServerName));
        if (socket.waitForConnected(500)) {
            // 已有实例运行，通知其显示界面
            socket.write("show");
            socket.flush();
            socket.waitForBytesWritten(1000);
            socket.disconnectFromServer();
            return 0;
        }
    }

    MainWindow w;

    if (headless) {
        w.startHeadless(port);
    } else {
        w.show();
    }

    return app.exec();
}
