#include "logger.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QTextStream>

// ============================================================================
//  单例
// ============================================================================

Logger *Logger::instance()
{
    static Logger s_instance;
    return &s_instance;
}

Logger::Logger(QObject *parent)
    : QObject(parent)
{
}

// ============================================================================
//  文件管理
// ============================================================================

bool Logger::openLogFile(const QString &filePath)
{
    QMutexLocker lock(&m_mutex);
    if (m_logFile.isOpen()) {
        m_logFile.close();
    }
    m_logFilePath = filePath;
    m_logFile.setFileName(filePath);
    return m_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
}

void Logger::closeLogFile()
{
    QMutexLocker lock(&m_mutex);
    if (m_logFile.isOpen()) {
        m_logFile.close();
    }
}

// ============================================================================
//  日志写入
// ============================================================================

void Logger::log(const QString &message)
{
    QString ts = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
    QString fileLine = QStringLiteral("[%1] %2").arg(ts, message);

    {
        QMutexLocker lock(&m_mutex);
        if (m_logFile.isOpen()) {
            rotateIfNeeded();
            QTextStream stream(&m_logFile);
            stream << fileLine << "\n";
            stream.flush();
        }
    }

    emit logged(message);
}

void Logger::setMaxFileSize(qint64 bytes)
{
    m_maxFileSize = bytes;
}

// ============================================================================
//  日志轮转
// ============================================================================

void Logger::rotateIfNeeded()
{
    if (!m_logFile.isOpen() || m_logFile.size() < m_maxFileSize) {
        return;
    }

    m_logFile.close();

    // 轮转：当前 → .1, .1 → .2, …, .4 → .5，删除 .5
    QFile::remove(m_logFilePath + ".5");
    for (int i = 4; i >= 1; --i) {
        QString oldName = m_logFilePath + "." + QString::number(i);
        QString newName = m_logFilePath + "." + QString::number(i + 1);
        QFile::rename(oldName, newName);
    }
    QFile::rename(m_logFilePath, m_logFilePath + ".1");

    m_logFile.setFileName(m_logFilePath);
    m_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
}
