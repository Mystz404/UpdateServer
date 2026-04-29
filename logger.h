#ifndef LOGGER_H
#define LOGGER_H

#include <QObject>
#include <QFile>
#include <QMutex>

/**
 * @brief 日志持久化管理器（单例）。
 *
 * 功能：
 * - 将日志同时写入文件和通过信号输出（供 UI 使用）；
 * - 按文件大小自动轮转（默认 10 MB），最多保留 5 个历史文件；
 * - 线程安全。
 */
class Logger : public QObject
{
    Q_OBJECT

public:
    static Logger *instance();

    /// 打开日志文件（追加写入模式）
    bool openLogFile(const QString &filePath);
    void closeLogFile();

    /// 记录一条日志（自动追加时间戳后写入文件，并通过 logged 信号通知）
    void log(const QString &message);

    /// 设置单个日志文件最大大小（字节），超过后自动轮转
    void setMaxFileSize(qint64 bytes);

signals:
    /// 有新日志时发出（参数为原始消息，不含时间戳）
    void logged(const QString &message);

private:
    explicit Logger(QObject *parent = nullptr);

    QFile   m_logFile;
    QMutex  m_mutex;
    qint64  m_maxFileSize = 10 * 1024 * 1024; // 10 MB
    QString m_logFilePath;

    void rotateIfNeeded();
};

#endif // LOGGER_H
