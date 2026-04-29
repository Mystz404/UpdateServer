#ifndef HTTPSERVER_H
#define HTTPSERVER_H

#include <QObject>
#include <QTcpServer>
#include <QSet>
#include <QMap>
#include <QElapsedTimer>

class AppUpdateManager;
class DocManager;
class UserManager;
class QTcpSocket;
class QTimer;
class QFile;

/**
 * @brief 异步文件传输状态。
 */
struct FileTransferState
{
    QFile  *file      = nullptr;
    qint64  remaining = 0;
    static constexpr qint64 CHUNK_SIZE = 64 * 1024; // 64 KB
};

/**
 * @brief 轻量级 HTTP 服务器。
 *
 * 基于 QTcpServer 实现，仅处理 GET 请求，支持路由：
 *
 * 1) GET /updates/{appId}.json → 静态升级元数据
 * 2) GET /download/{fileName}  → 升级包文件
 * 3) GET /status               → 服务器运行状态
 * 4) GET /                     → 健康检查
 *
 * 安全特性：路径穿越防护、连接数限制、空闲超时、异步大文件传输。
 *
 * 管理界面隔离说明：
 *   所有管理操作（CRUD）均通过本地 Qt GUI 完成，HTTP 仅暴露只读 GET 接口，
 *   无需额外的 HTTP 层管理接口鉴权。
 */
class HttpServer : public QObject
{
    Q_OBJECT

public:
    explicit HttpServer(AppUpdateManager *manager, QObject *parent = nullptr);
    ~HttpServer() override;

    /// 注入文档管理器（可选，不注入则文档路由返回 404）
    void setDocManager(DocManager *docManager);

    /// 注入用户管理器（可选，不注入则所有 /catalog 返回完整目录，/login 返回 503）
    void setUserManager(UserManager *userManager);

    /// 使令牌失效（通常在密码修改或手动踢出时调用）
    void invalidateToken(const QString &token);

    /// 启动监听
    bool start(quint16 port, QString &errorMessage);

    /// 停止监听并断开所有连接
    void stop();

    /// 当前是否正在监听
    bool isListening() const;

    /// 当前监听端口
    quint16 port() const;

    /// 获取本机外部访问基础 URL（如 http://192.168.1.100:8080）
    QString baseUrl() const;

    /// 设置对外访问基础 URL（如 https://download.bmshub.asia）
    void setPublicBaseUrl(const QString &urlText);

    /// 获取配置的对外访问基础 URL
    QString publicBaseUrl() const;

    /// 获取用于客户端元数据生成的基础 URL（优先 publicBaseUrl）
    QString effectiveBaseUrl() const;

    // ---- 连接管理 ----
    void setMaxConnections(int max);
    int  maxConnections() const;
    void setConnectionTimeout(int ms);
    int  connectionTimeout() const;
    int  activeConnectionCount() const;

    // ---- 统计 ----
    quint64 totalRequests() const;
    qint64  uptimeSeconds() const;

signals:
    /// 有新请求时发出，用于界面日志
    void requestReceived(const QString &method, const QString &path, int statusCode);
    /// 连接数告警
    void connectionWarning(const QString &message);

private slots:
    void onNewConnection();
    void onReadyRead();
    void onSocketDisconnected();
    void onIdleTimeout();
    void onBytesWritten();

private:
    void handleRequest(QTcpSocket *socket, const QByteArray &rawRequest);
    void handleLogin(QTcpSocket *socket, const QByteArray &rawRequest);
    /// 从原始请求头提取 Bearer token（找不到返回空字符串）
    QString extractBearerToken(const QByteArray &rawRequest) const;
    /// 根据 token 判断请求是否已登录（token 为空或无效→false）
    bool isAuthenticated(const QString &token) const;
    void sendResponse(QTcpSocket *socket, int statusCode,
                      const QByteArray &contentType, const QByteArray &body);
    void sendFile(QTcpSocket *socket,
                  const QString &filePath,
                  const QByteArray &contentType,
                  bool hasRange, qint64 rangeStart, qint64 rangeEnd);
    void send404(QTcpSocket *socket, const QString &path);
    void sendNextChunk(QTcpSocket *socket);
    void cleanupConnection(QTcpSocket *socket);
    void resetIdleTimer(QTcpSocket *socket);
    bool isPathSafe(const QString &baseDir, const QString &fileName) const;

    QTcpServer m_tcpServer;
    AppUpdateManager *m_manager     = nullptr;
    DocManager       *m_docManager  = nullptr;
    UserManager      *m_userManager = nullptr;
    quint16 m_port = 0;

    // ---- Token 表（内存存储，服务重启后失效）----
    // token → username
    QMap<QString, QString> m_tokens;

    // ---- 连接管理 ----
    QSet<QTcpSocket*>                     m_connections;
    QMap<QTcpSocket*, FileTransferState*> m_fileTransfers;
    QMap<QTcpSocket*, QTimer*>            m_idleTimers;
    QMap<QTcpSocket*, QByteArray>         m_requestBuffers; // TCP 分片请求累积缓冲
    int  m_maxConnections      = 200;
    int  m_connectionTimeoutMs = 30000; // 30 秒

    // ---- 统计 ----
    QElapsedTimer m_uptimeTimer;
    quint64 m_totalRequests = 0;

    // 对外域名基址（可选），用于生成给客户端的 URL
    QString m_publicBaseUrl;
};

#endif // HTTPSERVER_H
