#include "httpserver.h"
#include "appupdatemanager.h"
#include "docmanager.h"
#include "logger.h"
#include "usermanager.h"

#include <QTcpSocket>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkInterface>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QRegularExpression>
#include <QTimer>
#include <QUuid>
#include <QtGlobal>

// 最大请求大小（16 KB；含 POST /login 正文）
static constexpr qint64 MAX_REQUEST_SIZE = 16 * 1024;

// 发送缓冲区高水位（超过时暂停写入，等待 bytesWritten）
static constexpr qint64 WRITE_BUFFER_HIGH = 256 * 1024;

namespace {
QString normalizeBaseUrl(const QString &urlText)
{
    QString candidate = urlText.trimmed();
    if (candidate.isEmpty()) {
        return {};
    }

    // 兼容仅填写域名/域名:端口的输入。
    if (!candidate.contains(QStringLiteral("://"))) {
        candidate.prepend(QStringLiteral("http://"));
    }

    QUrl url(candidate);
    if (!url.isValid() || url.host().isEmpty()) {
        return {};
    }

    QString path = url.path();
    if (path.isEmpty()) {
        path = QStringLiteral("/");
    }
    if (!path.endsWith('/')) {
        path += '/';
    }
    url.setPath(path);
    return url.toString(QUrl::FullyEncoded);
}
}

// ---------------------------------------------------------------------------
// 构造
// ---------------------------------------------------------------------------

HttpServer::HttpServer(AppUpdateManager *manager, QObject *parent)
    : QObject(parent), m_manager(manager)
{
    connect(&m_tcpServer, &QTcpServer::newConnection,
            this, &HttpServer::onNewConnection);
    m_uptimeTimer.start();
}

HttpServer::~HttpServer()
{
    stop();
}

// ---------------------------------------------------------------------------
// 启动 / 停止
// ---------------------------------------------------------------------------

void HttpServer::setDocManager(DocManager *docManager)
{
    m_docManager = docManager;
}

void HttpServer::setUserManager(UserManager *userManager)
{
    m_userManager = userManager;
}

void HttpServer::invalidateToken(const QString &token)
{
    m_tokens.remove(token);
}

// ---------------------------------------------------------------------------
// 认证辅助
// ---------------------------------------------------------------------------

QString HttpServer::extractBearerToken(const QByteArray &rawRequest) const
{
    // 从 Authorization: Bearer <token> 请求头提取 token
    static QRegularExpression rxBearer(
        QStringLiteral("(?:\\r\\n|\\n)Authorization\\s*:\\s*Bearer\\s+(\\S+)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = rxBearer.match(QString::fromLatin1(rawRequest));
    if (m.hasMatch()) {
        return m.captured(1).trimmed();
    }
    return {};
}

bool HttpServer::isAuthenticated(const QString &token) const
{
    return !token.isEmpty() && m_tokens.contains(token);
}

// ---------------------------------------------------------------------------
// POST /login 处理
// ---------------------------------------------------------------------------

void HttpServer::handleLogin(QTcpSocket *socket, const QByteArray &rawRequest)
{
    const int bodyStart = rawRequest.indexOf("\r\n\r\n");
    if (bodyStart < 0) {
        sendResponse(socket, 400, "application/json; charset=utf-8",
                     "{\"error\":\"\u8bf7\u6c42\u6b63\u6587\u7f3a\u5c11\"}");
        emit requestReceived(QStringLiteral("POST"), QStringLiteral("/login"), 400);
        return;
    }
    const QByteArray body = rawRequest.mid(bodyStart + 4);

    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        sendResponse(socket, 400, "application/json; charset=utf-8",
                     "{\"error\":\"JSON \u683c\u5f0f\u9519\u8bef\"}");
        emit requestReceived(QStringLiteral("POST"), QStringLiteral("/login"), 400);
        return;
    }

    const QJsonObject obj = doc.object();
    const QString username = obj.value(QStringLiteral("username")).toString().trimmed();
    const QString password = obj.value(QStringLiteral("password")).toString();

    if (username.isEmpty() || password.isEmpty()) {
        sendResponse(socket, 400, "application/json; charset=utf-8",
                     "{\"error\":\"\u7528\u6237\u540d\u6216\u5bc6\u7801\u4e0d\u80fd\u4e3a\u7a7a\"}");
        emit requestReceived(QStringLiteral("POST"), QStringLiteral("/login"), 400);
        return;
    }

    if (!m_userManager) {
        sendResponse(socket, 503, "application/json; charset=utf-8",
                     "{\"error\":\"\u7528\u6237\u7ba1\u7406\u672a\u521d\u59cb\u5316\"}");
        emit requestReceived(QStringLiteral("POST"), QStringLiteral("/login"), 503);
        return;
    }

    const User user = m_userManager->authenticate(username, password);
    if (user.id.isEmpty()) {
        sendResponse(socket, 401, "application/json; charset=utf-8",
                     "{\"error\":\"\u7528\u6237\u540d\u6216\u5bc6\u7801\u9519\u8bef\"}");
        emit requestReceived(QStringLiteral("POST"), QStringLiteral("/login"), 401);
        return;
    }

    const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_tokens.insert(token, user.username);

    QJsonObject resp;
    resp.insert(QStringLiteral("token"),    token);
    resp.insert(QStringLiteral("username"), user.username);
    resp.insert(QStringLiteral("role"),     user.role);
    sendResponse(socket, 200, "application/json; charset=utf-8",
                 QJsonDocument(resp).toJson(QJsonDocument::Compact));
    emit requestReceived(QStringLiteral("POST"), QStringLiteral("/login"), 200);
}

// ---------------------------------------------------------------------------
// 启动 / 停止
// ---------------------------------------------------------------------------

bool HttpServer::start(quint16 port, QString &errorMessage)
{
    if (m_tcpServer.isListening()) {
        m_tcpServer.close();
    }
    if (!m_tcpServer.listen(QHostAddress::Any, port)) {
        errorMessage = QStringLiteral("启动监听失败（端口 %1）：%2")
                           .arg(port)
                           .arg(m_tcpServer.errorString());
        return false;
    }
    m_port = m_tcpServer.serverPort();
    m_totalRequests = 0;
    m_uptimeTimer.restart();
    return true;
}

void HttpServer::stop()
{
    m_tcpServer.close();
    // 断开所有活动连接
    const auto sockets = m_connections;
    for (QTcpSocket *s : sockets) {
        cleanupConnection(s);
        s->abort();
    }
}

bool HttpServer::isListening() const
{
    return m_tcpServer.isListening();
}

quint16 HttpServer::port() const
{
    return m_port;
}

QString HttpServer::baseUrl() const
{
    // 找到第一个非回环 IPv4 地址
    const auto allAddresses = QNetworkInterface::allAddresses();
    for (const QHostAddress &addr : allAddresses) {
        if (addr.protocol() == QAbstractSocket::IPv4Protocol && !addr.isLoopback()) {
            return QStringLiteral("http://%1:%2").arg(addr.toString()).arg(m_port);
        }
    }
    return QStringLiteral("http://127.0.0.1:%1").arg(m_port);
}

void HttpServer::setPublicBaseUrl(const QString &urlText)
{
    m_publicBaseUrl = normalizeBaseUrl(urlText);
}

QString HttpServer::publicBaseUrl() const
{
    return m_publicBaseUrl;
}

QString HttpServer::effectiveBaseUrl() const
{
    if (!m_publicBaseUrl.trimmed().isEmpty()) {
        return m_publicBaseUrl;
    }
    return baseUrl();
}

// ---------------------------------------------------------------------------
// 连接管理参数
// ---------------------------------------------------------------------------

void HttpServer::setMaxConnections(int max) { m_maxConnections = max; }
int  HttpServer::maxConnections() const     { return m_maxConnections; }
void HttpServer::setConnectionTimeout(int ms) { m_connectionTimeoutMs = ms; }
int  HttpServer::connectionTimeout() const  { return m_connectionTimeoutMs; }
int  HttpServer::activeConnectionCount() const { return m_connections.size(); }
quint64 HttpServer::totalRequests() const   { return m_totalRequests; }
qint64  HttpServer::uptimeSeconds() const   { return m_uptimeTimer.elapsed() / 1000; }

// ---------------------------------------------------------------------------
// 连接 & 读取
// ---------------------------------------------------------------------------

void HttpServer::onNewConnection()
{
    while (QTcpSocket *socket = m_tcpServer.nextPendingConnection()) {
        // ---- 连接数限制 ----
        if (m_connections.size() >= m_maxConnections) {
            Logger::instance()->log(QStringLiteral("连接数已达上限 (%1)，拒绝新连接: %2")
                                        .arg(m_maxConnections)
                                        .arg(socket->peerAddress().toString()));
            emit connectionWarning(QStringLiteral("连接数达到上限 %1").arg(m_maxConnections));
            socket->abort();
            socket->deleteLater();
            continue;
        }

        m_connections.insert(socket);
        connect(socket, &QTcpSocket::readyRead,    this, &HttpServer::onReadyRead);
        connect(socket, &QTcpSocket::disconnected,  this, &HttpServer::onSocketDisconnected);
        connect(socket, &QTcpSocket::bytesWritten,  this, &HttpServer::onBytesWritten);

        // ---- 空闲超时 ----
        QTimer *timer = new QTimer(this);
        timer->setSingleShot(true);
        timer->setInterval(m_connectionTimeoutMs);
        timer->setProperty("socket", QVariant::fromValue<QObject*>(socket));
        connect(timer, &QTimer::timeout, this, &HttpServer::onIdleTimeout);
        m_idleTimers.insert(socket, timer);
        timer->start();

        // 连接数告警（80% 阈值）
        if (m_connections.size() >= m_maxConnections * 8 / 10) {
            emit connectionWarning(QStringLiteral("活动连接数已达 %1/%2 (80%%)")
                                       .arg(m_connections.size())
                                       .arg(m_maxConnections));
        }
    }
}

void HttpServer::onReadyRead()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket) return;

    resetIdleTimer(socket);

    // 追加新到达的数据到缓冲区（处理 TCP 分片）
    m_requestBuffers[socket].append(socket->readAll());
    QByteArray &buf = m_requestBuffers[socket];

    // ---- 请求大小限制（累积后检查）----
    if (buf.size() > MAX_REQUEST_SIZE) {
        m_requestBuffers.remove(socket);
        sendResponse(socket, 413, "application/json; charset=utf-8",
                     "{\"error\":\"\u8BF7\u6C42\u5934\u8FC7\u5927\"}");
        return;
    }

    // 等待完整的 HTTP 请求头（以 \r\n\r\n 结尾）
    const int headerEnd = buf.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        return; // headers 尚未到齐，等待下一个 readyRead
    }

    // 解析 Content-Length，确认 body 也完全到达
    int contentLength = 0;
    const QByteArray headers = buf.left(headerEnd);
    const int clPos = headers.toLower().indexOf("content-length:");
    if (clPos >= 0) {
        const int clEnd = headers.indexOf('\n', clPos);
        contentLength = headers.mid(clPos + 15, clEnd - clPos - 15).trimmed().toInt();
    }
    const int totalExpected = headerEnd + 4 + contentLength;
    if (buf.size() < totalExpected) {
        return; // body 尚未到齐，等待下一个 readyRead
    }

    QByteArray raw = buf.left(totalExpected);
    m_requestBuffers.remove(socket);
    handleRequest(socket, raw);
}

void HttpServer::onSocketDisconnected()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket) return;
    cleanupConnection(socket);
    socket->deleteLater();
}

void HttpServer::onIdleTimeout()
{
    QTimer *timer = qobject_cast<QTimer *>(sender());
    if (!timer) return;
    QTcpSocket *socket = qobject_cast<QTcpSocket *>(
        timer->property("socket").value<QObject*>());
    if (!socket) return;
    Logger::instance()->log(QStringLiteral("连接超时断开: %1").arg(socket->peerAddress().toString()));
    cleanupConnection(socket);
    socket->abort();
    socket->deleteLater();
}

void HttpServer::onBytesWritten()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket) return;
    if (m_fileTransfers.contains(socket)) {
        sendNextChunk(socket);
    }
}

// ---------------------------------------------------------------------------
// 路径安全检查
// ---------------------------------------------------------------------------

bool HttpServer::isPathSafe(const QString &baseDir, const QString &fileName) const
{
    // 拒绝包含目录穿越特征的文件名
    if (fileName.isEmpty()
        || fileName.contains(QStringLiteral(".."))
        || fileName.contains('/')
        || fileName.contains('\\')) {
        return false;
    }

    // 二次验证：解析后的绝对路径必须仍在 baseDir 内
    QString resolved = QDir::cleanPath(QDir(baseDir).absoluteFilePath(fileName));
    QString baseClean = QDir::cleanPath(QFileInfo(baseDir).absoluteFilePath());
    return resolved.startsWith(baseClean + '/') || resolved.startsWith(baseClean + '\\');
}

// ---------------------------------------------------------------------------
// 请求分发
// ---------------------------------------------------------------------------

void HttpServer::handleRequest(QTcpSocket *socket, const QByteArray &rawRequest)
{
    ++m_totalRequests;
    // 解析请求行: "GET /path HTTP/1.1\r\n..."
    int lineEnd = rawRequest.indexOf("\r\n");
    if (lineEnd < 0) lineEnd = rawRequest.indexOf('\n');
    QByteArray requestLine = (lineEnd > 0) ? rawRequest.left(lineEnd) : rawRequest;

    QList<QByteArray> parts = requestLine.split(' ');
    if (parts.size() < 2) {
        Logger::instance()->log(QStringLiteral("收到格式错误的请求行，已拒绝"));
        sendResponse(socket, 400, "application/json; charset=utf-8",
                     "{\"error\":\"\u8BF7\u6C42\u884C\u683C\u5F0F\u9519\u8BEF\"}");
        emit requestReceived(QStringLiteral("UNKNOWN"), QStringLiteral("/"), 400);
        return;
    }

    QString method = QString::fromLatin1(parts[0]);
    QString path = QUrl::fromPercentEncoding(parts[1]);

    // ------ POST /login 优先处理（在 GET 限制检查之前）------
    if (method.toUpper() == QStringLiteral("POST") && path == QStringLiteral("/login")) {
        handleLogin(socket, rawRequest);
        return;
    }

    // 其余路由仅支持 GET
    if (method.toUpper() != QStringLiteral("GET")) {
        QByteArray body = QStringLiteral("{\"error\":\"\u4ec5\u652f\u6301 GET \u8bf7\u6c42\",\"method\":\"%1\"}")
                              .arg(method)
                              .toUtf8();
        sendResponse(socket, 405, "application/json; charset=utf-8", body);
        emit requestReceived(method, path, 405);
        return;
    }

    qint64 rangeStart = 0;
    qint64 rangeEnd = -1;
    bool hasRange = false;
    static QRegularExpression rxRange(R"((?:\r\n|\n)Range\s*:\s*bytes=(\d*)-(\d*)\s*(?:\r\n|\n))",
                                      QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch rangeMatch = rxRange.match(QString::fromLatin1(rawRequest));
    if (rangeMatch.hasMatch()) {
        hasRange = true;
        const QString startText = rangeMatch.captured(1).trimmed();
        const QString endText = rangeMatch.captured(2).trimmed();
        if (!startText.isEmpty()) {
            rangeStart = startText.toLongLong();
        }
        if (!endText.isEmpty()) {
            rangeEnd = endText.toLongLong();
        }
    }

    // ------ 路由匹配 ------

    // 1) GET /updates/{appId}.json — 提供预生成的静态元数据文件
    static QRegularExpression rxUpdate(R"(^/updates/([^/]+)\.json$)");
    QRegularExpressionMatch m = rxUpdate.match(path);
    if (m.hasMatch()) {
        QString appId = m.captured(1);
        QString updatesDir = m_manager->updatesDir();
        QString jsonName = appId + QStringLiteral(".json");

        // ---- 路径穿越防护 ----
        if (!isPathSafe(updatesDir, jsonName)) {
            Logger::instance()->log(QStringLiteral("路径穿越攻击被拦截: %1").arg(path));
            send404(socket, path);
            return;
        }

        QString filePath = QDir(updatesDir).absoluteFilePath(jsonName);
        QFileInfo fi(filePath);
        if (!fi.exists() || !fi.isFile()) {
            send404(socket, path);
            return;
        }
        sendFile(socket, filePath, "application/json; charset=utf-8",
                 hasRange, rangeStart, rangeEnd);
        emit requestReceived(method, path, hasRange ? 206 : 200);
        return;
    }

    // 2) GET /download/{appId}/{fileName}
    static QRegularExpression rxDownload(R"(^/download/([^/]+)/([^/]+)$)");
    m = rxDownload.match(path);
    if (m.hasMatch()) {
        QString appId = m.captured(1);
        QString fileName = m.captured(2);

        // ---- 路径穿越防护 ----
        if (!isPathSafe(m_manager->packagesDir(), appId)) {
            Logger::instance()->log(QStringLiteral("路径穿越攻击被拦截: %1").arg(path));
            send404(socket, path);
            return;
        }
        QString appPkgDir = QDir(m_manager->packagesDir()).absoluteFilePath(appId);
        if (!isPathSafe(appPkgDir, fileName)) {
            Logger::instance()->log(QStringLiteral("路径穿越攻击被拦截: %1").arg(path));
            send404(socket, path);
            return;
        }

        QString filePath = QDir(appPkgDir).absoluteFilePath(fileName);
        QFileInfo fi(filePath);
        if (!fi.exists() || !fi.isFile()) {
            QByteArray body = QStringLiteral("{\"error\":\"升级包文件不存在\",\"requestPath\":\"%1\"}")
                                  .arg(path)
                                  .toUtf8();
            sendResponse(socket, 404, "application/json; charset=utf-8", body);
            emit requestReceived(method, path, 404);
            return;
        }

        // 根据后缀确定 Content-Type
        QByteArray ct = "application/octet-stream";
        if (fileName.endsWith(".json", Qt::CaseInsensitive))
            ct = "application/json; charset=utf-8";
        else if (fileName.endsWith(".zip", Qt::CaseInsensitive))
            ct = "application/zip";
        else if (fileName.endsWith(".exe", Qt::CaseInsensitive))
            ct = "application/x-msdownload";

        sendFile(socket, filePath, ct, hasRange, rangeStart, rangeEnd);
        emit requestReceived(method, path, hasRange ? 206 : 200);
        return;
    }

    // 2.5) GET /dep/{appId}?file={relativePath} — 下载单个依赖文件
    //      path 可能包含查询字符串，先分离
    {
        QString depPath = path;
        QString depQuery;
        const int qIdx = path.indexOf('?');
        if (qIdx >= 0) {
            depPath = path.left(qIdx);
            depQuery = path.mid(qIdx + 1);
        }
        static QRegularExpression rxDep(R"(^/dep/([^/]+)$)");
        m = rxDep.match(depPath);
        if (m.hasMatch()) {
            const QString appId = m.captured(1);
            if (!m_manager->hasEntry(appId)) {
                send404(socket, path);
                return;
            }
            const AppUpdateEntry &entry = m_manager->entry(appId);
            if (entry.depsDir.isEmpty() || !QDir(entry.depsDir).exists()) {
                sendResponse(socket, 404, "application/json; charset=utf-8",
                             QStringLiteral("{\"error\":\"该应用未配置依赖文件目录\"}").toUtf8());
                emit requestReceived(method, path, 404);
                return;
            }

            // 从查询字符串中提取 file 参数
            QString relFile;
            for (const QString &param : depQuery.split('&', Qt::SkipEmptyParts)) {
                if (param.startsWith(QStringLiteral("file="))) {
                    relFile = QUrl::fromPercentEncoding(param.mid(5).toUtf8());
                    break;
                }
            }
            if (relFile.isEmpty() || relFile.contains(QStringLiteral(".."))) {
                sendResponse(socket, 400, "application/json; charset=utf-8",
                             QStringLiteral("{\"error\":\"缺少 file 参数或路径非法\"}").toUtf8());
                emit requestReceived(method, path, 400);
                return;
            }

            // 去除开头的 ./ 前缀，仅保留相对文件名
            QString cleanedFile = relFile;
            while (cleanedFile.startsWith(QStringLiteral("./")))
                cleanedFile = cleanedFile.mid(2);
            while (cleanedFile.startsWith(QStringLiteral(".\\")))
                cleanedFile = cleanedFile.mid(2);
            if (cleanedFile.isEmpty()) {
                sendResponse(socket, 400, "application/json; charset=utf-8",
                             QStringLiteral("{\"error\":\"file 参数无效\"}").toUtf8());
                emit requestReceived(method, path, 400);
                return;
            }

            // 安全校验：解析后路径必须在 depsDir 内
            const QString resolved = QDir::cleanPath(QDir(entry.depsDir).absoluteFilePath(cleanedFile));
            const QString baseClean = QDir::cleanPath(entry.depsDir);
            if (!resolved.startsWith(baseClean + '/') && !resolved.startsWith(baseClean + '\\')) {
                Logger::instance()->log(QStringLiteral("依赖文件路径穿越被拦截: %1").arg(relFile));
                send404(socket, path);
                return;
            }

            QFileInfo fi(resolved);
            if (!fi.exists() || !fi.isFile()) {
                Logger::instance()->log(QStringLiteral("依赖文件不存在: %1 (解析路径: %2)")
                                            .arg(relFile, QDir::toNativeSeparators(resolved)));
                QByteArray body = QStringLiteral("{\"error\":\"依赖文件不存在\",\"file\":\"%1\"}")
                                      .arg(relFile).toUtf8();
                sendResponse(socket, 404, "application/json; charset=utf-8", body);
                emit requestReceived(method, path, 404);
                return;
            }

            sendFile(socket, resolved, "application/octet-stream", hasRange, rangeStart, rangeEnd);
            emit requestReceived(method, path, hasRange ? 206 : 200);
            return;
        }
    }

    // 3) GET /status — 服务器运行状态（监控）
    if (path == "/status") {
        QJsonObject status;
        status.insert("status", "ok");
        status.insert("server", "UpdateServer");
        status.insert("uptimeSeconds", uptimeSeconds());
        status.insert("activeConnections", activeConnectionCount());
        status.insert("maxConnections", m_maxConnections);
        status.insert("totalRequests", static_cast<qint64>(m_totalRequests));
        status.insert("connectionTimeoutMs", m_connectionTimeoutMs);
        status.insert("appCount", m_manager->entries().size());

        QByteArray body = QJsonDocument(status).toJson(QJsonDocument::Compact);
        sendResponse(socket, 200, "application/json; charset=utf-8", body);
        emit requestReceived(method, path, 200);
        return;
    }

    // 5) GET /catalog — 应用清单（根据登录状态过滤 requiresLogin 条目）
    if (path == "/catalog") {
        const QString token   = extractBearerToken(rawRequest);
        const bool    loggedIn = isAuthenticated(token);

        // 获取当前用户（用于 allowedApps 过滤）
        QStringList allowedApps; // 空=允许所有
        if (loggedIn && m_userManager) {
            const QString username = m_tokens.value(token);
            const User currentUser = m_userManager->userByName(username);
            allowedApps = currentUser.allowedApps; // 空=允许所有
        }

        const QString srvBaseUrl = effectiveBaseUrl();
        const QJsonArray fullCatalog = m_manager->buildCatalog(srvBaseUrl);

        QJsonArray filtered;
        for (const QJsonValue &v : fullCatalog) {
            const QJsonObject item = v.toObject();
            const bool needsLogin = item.value(QStringLiteral("requiresLogin")).toBool(false);
            if (needsLogin && !loggedIn) continue; // 未登录跳过受限应用

            // 按用户 allowedApps 进一步过滤（allowedApps 为空=允许全部）
            if (loggedIn && !allowedApps.isEmpty()) {
                const QString appId = item.value(QStringLiteral("appId")).toString();
                if (!allowedApps.contains(appId)) continue;
            }

            // 对外暴露时去除 requiresLogin 字段（客户端不需要）
            QJsonObject out = item;
            out.remove(QStringLiteral("requiresLogin"));
            filtered.append(out);
        }

        QByteArray body = QJsonDocument(filtered).toJson(QJsonDocument::Compact);
        sendResponse(socket, 200, "application/json; charset=utf-8", body);
        emit requestReceived(method, path, 200);
        return;
    }

    // 6) GET /history/{appId} — 历史版本列表
    static QRegularExpression rxHistory(R"(^/history/([^/]+)$)");
    m = rxHistory.match(path);
    if (m.hasMatch()) {
        QString appId = m.captured(1);
        if (!m_manager->hasEntry(appId)) {
            send404(socket, path);
            return;
        }
        QString srvBaseUrl = effectiveBaseUrl();
        QJsonObject histMeta = m_manager->buildHistoryMeta(appId, srvBaseUrl);
        QByteArray body = QJsonDocument(histMeta).toJson(QJsonDocument::Compact);
        sendResponse(socket, 200, "application/json; charset=utf-8", body);
        emit requestReceived(method, path, 200);
        return;
    }

    // 7) GET /download/history/{appId}/{fileName} — 下载历史版本文件
    static QRegularExpression rxHistDl(R"(^/download/history/([^/]+)/([^/]+)$)");
    m = rxHistDl.match(path);
    if (m.hasMatch()) {
        QString appId = m.captured(1);
        QString fileName = m.captured(2);
        QString histDir = m_manager->historyDirForApp(appId);
        if (!isPathSafe(histDir, fileName)) {
            Logger::instance()->log(QStringLiteral("路径穿越攻击被拦截（历史版本）: %1").arg(path));
            send404(socket, path);
            return;
        }

        QString filePath = QDir(histDir).absoluteFilePath(fileName);
        QFileInfo fi(filePath);
        if (!fi.exists() || !fi.isFile()) {
            send404(socket, path);
            return;
        }

        QByteArray ct = "application/octet-stream";
        if (fileName.endsWith(".exe", Qt::CaseInsensitive))
            ct = "application/x-msdownload";
        else if (fileName.endsWith(".zip", Qt::CaseInsensitive))
            ct = "application/zip";

        sendFile(socket, filePath, ct, hasRange, rangeStart, rangeEnd);
        emit requestReceived(method, path, hasRange ? 206 : 200);
        return;
    }

    // 4) GET / — 简单健康检查
    if (path == "/") {
        QByteArray body = "{\"status\":\"ok\",\"server\":\"UpdateServer\"}";
        sendResponse(socket, 200, "application/json; charset=utf-8", body);
        emit requestReceived(method, path, 200);
        return;
    }

    // 8) GET /docs/catalog — 文档清单（根据登录状态过滤 requiresLogin 条目）
    if (path == QStringLiteral("/docs/catalog")) {
        if (!m_docManager) { send404(socket, path); return; }

        const QString token    = extractBearerToken(rawRequest);
        const bool    loggedIn = isAuthenticated(token);

        const QJsonArray fullCatalog = m_docManager->buildCatalog(effectiveBaseUrl());
        QJsonArray filtered;
        for (const QJsonValue &v : fullCatalog) {
            const QJsonObject item = v.toObject();
            const bool needsLogin = item.value(QStringLiteral("requiresLogin")).toBool(false);
            if (needsLogin && !loggedIn) continue;
            QJsonObject out = item;
            out.remove(QStringLiteral("requiresLogin"));
            filtered.append(out);
        }

        sendResponse(socket, 200, "application/json; charset=utf-8",
                     QJsonDocument(filtered).toJson(QJsonDocument::Compact));
        emit requestReceived(method, path, 200);
        return;
    }

    // 9) GET /docs/download/{docId}/{fileName} — 下载文档文件
    static QRegularExpression rxDocDl(R"(^/docs/download/([^/]+)/([^/]+)$)");
    QRegularExpressionMatch mdoc = rxDocDl.match(path);
    if (mdoc.hasMatch()) {
        if (!m_docManager) { send404(socket, path); return; }
        const QString docId   = mdoc.captured(1);
        const QString docFile = mdoc.captured(2);
        if (!isPathSafe(m_docManager->docsDir(), docId)) {
            Logger::instance()->log(QStringLiteral("路径穿越攻击被拦截(文档): %1").arg(path));
            send404(socket, path); return;
        }
        const QString docSubDir = QDir(m_docManager->docsDir()).absoluteFilePath(docId);
        if (!isPathSafe(docSubDir, docFile)) {
            Logger::instance()->log(QStringLiteral("路径穿越攻击被拦截(文档文件): %1").arg(path));
            send404(socket, path); return;
        }
        const QString filePath = QDir(docSubDir).absoluteFilePath(docFile);
        if (!QFileInfo(filePath).isFile()) { send404(socket, path); return; }
        // 根据文件后缀动态确定 MIME 类型，支持 PDF/Word/Excel/PPT/图片等格式
        const QString ext = QFileInfo(docFile).suffix().toLower();
        QByteArray ct = "application/octet-stream";
        if      (ext == "pdf")                    ct = "application/pdf";
        else if (ext == "docx")                   ct = "application/vnd.openxmlformats-officedocument.wordprocessingml.document";
        else if (ext == "doc")                    ct = "application/msword";
        else if (ext == "xlsx")                   ct = "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet";
        else if (ext == "xls")                    ct = "application/vnd.ms-excel";
        else if (ext == "pptx")                   ct = "application/vnd.openxmlformats-officedocument.presentationml.presentation";
        else if (ext == "ppt")                    ct = "application/vnd.ms-powerpoint";
        else if (ext == "txt")                    ct = "text/plain; charset=utf-8";
        else if (ext == "md")                     ct = "text/markdown; charset=utf-8";
        else if (ext == "png")                    ct = "image/png";
        else if (ext == "jpg" || ext == "jpeg")   ct = "image/jpeg";
        else if (ext == "bmp")                    ct = "image/bmp";
        else if (ext == "gif")                    ct = "image/gif";
        sendFile(socket, filePath, ct, hasRange, rangeStart, rangeEnd);
        emit requestReceived(method, path, hasRange ? 206 : 200);
        return;
    }

    // 没有匹配的路由
    send404(socket, path);
}

// ---------------------------------------------------------------------------
// 响应工具方法
// ---------------------------------------------------------------------------

void HttpServer::sendResponse(QTcpSocket *socket, int statusCode,
                               const QByteArray &contentType, const QByteArray &body)
{
    static const QMap<int, QByteArray> phrases = {
        {200, "OK"}, {206, "Partial Content"},
        {400, "Bad Request"}, {404, "Not Found"},
        {405, "Method Not Allowed"}, {413, "Payload Too Large"},
        {416, "Requested Range Not Satisfiable"}, {500, "Internal Server Error"},
        {503, "Service Unavailable"}
    };
    QByteArray phrase = phrases.value(statusCode, "Unknown");

    QByteArray header;
    header.append("HTTP/1.1 " + QByteArray::number(statusCode) + " " + phrase + "\r\n");
    header.append("Content-Type: " + contentType + "\r\n");
    header.append("Content-Length: " + QByteArray::number(body.size()) + "\r\n");
    header.append("Connection: close\r\n");
    header.append("Access-Control-Allow-Origin: *\r\n");
    header.append("Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n");
    header.append("Access-Control-Allow-Headers: Content-Type, Authorization\r\n");
    header.append("\r\n");

    socket->write(header);
    socket->write(body);
    socket->flush();
    socket->disconnectFromHost();
}

void HttpServer::sendFile(QTcpSocket *socket,
                           const QString &filePath,
                           const QByteArray &contentType,
                           bool hasRange,
                           qint64 rangeStart,
                           qint64 rangeEnd)
{
    const QString absFilePath = QFileInfo(filePath).absoluteFilePath();
    QFile *file = new QFile(absFilePath);
    if (!file->open(QIODevice::ReadOnly)) {
        delete file;
        sendResponse(socket, 500, "application/json; charset=utf-8",
                     "{\"error\":\"无法读取文件\"}");
        return;
    }

    const qint64 fileSize = file->size();
    qint64 start = 0;
    qint64 end = fileSize - 1;

    if (hasRange) {
        if (rangeStart >= fileSize) {
            delete file;
            QByteArray body = "{\"error\":\"Range 超出文件大小\"}";
            QByteArray header;
            header.append("HTTP/1.1 416 Requested Range Not Satisfiable\r\n");
            header.append("Content-Type: application/json; charset=utf-8\r\n");
            header.append("Content-Range: bytes */" + QByteArray::number(fileSize) + "\r\n");
            header.append("Content-Length: " + QByteArray::number(body.size()) + "\r\n");
            header.append("Connection: close\r\n\r\n");
            socket->write(header);
            socket->write(body);
            socket->flush();
            socket->disconnectFromHost();
            return;
        }
        start = qMax<qint64>(0, rangeStart);
        if (rangeEnd >= 0) end = qMin(rangeEnd, fileSize - 1);
        if (end < start) end = fileSize - 1;
    }

    const qint64 contentLength = end - start + 1;
    if (!file->seek(start)) {
        delete file;
        sendResponse(socket, 500, "application/json; charset=utf-8",
                     "{\"error\":\"文件定位失败\"}");
        return;
    }

    // 构建响应头
    QByteArray header;
    if (hasRange) {
        header.append("HTTP/1.1 206 Partial Content\r\n");
        header.append("Content-Range: bytes "
                      + QByteArray::number(start) + "-"
                      + QByteArray::number(end) + "/"
                      + QByteArray::number(fileSize) + "\r\n");
    } else {
        header.append("HTTP/1.1 200 OK\r\n");
    }
    header.append("Content-Type: " + contentType + "\r\n");
    header.append("Content-Length: " + QByteArray::number(contentLength) + "\r\n");
    header.append("Accept-Ranges: bytes\r\n");
    header.append("Connection: close\r\n");
    header.append("Access-Control-Allow-Origin: *\r\n");
    header.append("\r\n");

    socket->write(header);

    // ---- 异步分块传输 ----
    FileTransferState *state = new FileTransferState;
    state->file = file;
    state->remaining = contentLength;
    m_fileTransfers.insert(socket, state);

    // 立即发送第一批数据
    sendNextChunk(socket);
}

void HttpServer::send404(QTcpSocket *socket, const QString &path)
{
    QByteArray body = QStringLiteral("{\"error\":\"资源不存在\",\"path\":\"%1\"}").arg(path).toUtf8();
    sendResponse(socket, 404, "application/json; charset=utf-8", body);
    emit requestReceived("GET", path, 404);
}

// ---------------------------------------------------------------------------
// 异步文件传输
// ---------------------------------------------------------------------------

void HttpServer::sendNextChunk(QTcpSocket *socket)
{
    auto it = m_fileTransfers.find(socket);
    if (it == m_fileTransfers.end()) return;

    FileTransferState *state = it.value();

    // 当发送缓冲区低于高水位时继续写入
    while (state->remaining > 0 && socket->bytesToWrite() < WRITE_BUFFER_HIGH) {
        const qint64 toRead = qMin(FileTransferState::CHUNK_SIZE, state->remaining);
        QByteArray chunk = state->file->read(toRead);
        if (chunk.isEmpty()) break;

        socket->write(chunk);
        state->remaining -= chunk.size();
        resetIdleTimer(socket);
    }

    if (state->remaining <= 0) {
        // 传输完成
        state->file->close();
        delete state->file;
        delete state;
        m_fileTransfers.remove(socket);
        socket->flush();
        socket->disconnectFromHost();
    }
    // else: 等待 bytesWritten 信号触发下一轮发送
}

// ---------------------------------------------------------------------------
// 连接管理
// ---------------------------------------------------------------------------

void HttpServer::cleanupConnection(QTcpSocket *socket)
{
    // 清理未完成的请求缓冲
    m_requestBuffers.remove(socket);

    // 清理文件传输状态
    auto it = m_fileTransfers.find(socket);
    if (it != m_fileTransfers.end()) {
        FileTransferState *state = it.value();
        if (state->file) {
            state->file->close();
            delete state->file;
        }
        delete state;
        m_fileTransfers.erase(it);
    }

    // 清理空闲定时器
    auto timerIt = m_idleTimers.find(socket);
    if (timerIt != m_idleTimers.end()) {
        timerIt.value()->stop();
        timerIt.value()->deleteLater();
        m_idleTimers.erase(timerIt);
    }

    m_connections.remove(socket);
}

void HttpServer::resetIdleTimer(QTcpSocket *socket)
{
    auto it = m_idleTimers.find(socket);
    if (it != m_idleTimers.end()) {
        it.value()->start(m_connectionTimeoutMs);
    }
}
