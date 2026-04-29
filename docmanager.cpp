#include "docmanager.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

DocManager::DocManager(QObject *parent) : QObject(parent) {}

bool DocManager::load(const QString &configPath, QString &errorMessage)
{
    m_configPath = configPath;
    const QString absPath = QFileInfo(configPath).absoluteFilePath();
    QFile file(absPath);
    if (!file.exists()) return true; // 首次启动，空配置

    if (!file.open(QIODevice::ReadOnly)) {
        errorMessage = QStringLiteral("无法打开文档配置文件: %1").arg(absPath);
        return false;
    }
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    file.close();
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        errorMessage = QStringLiteral("文档配置 JSON 格式错误: %1").arg(err.errorString());
        return false;
    }

    const QJsonObject root = doc.object();
    m_docsDir = root.value(QStringLiteral("docsDir")).toString().trimmed();
    if (m_docsDir.isEmpty())
        m_docsDir = QFileInfo(configPath).absolutePath() + QStringLiteral("/docs");

    m_entries.clear();
    for (const QJsonValue &v : root.value(QStringLiteral("docs")).toArray()) {
        DocEntry e = DocEntry::fromJson(v.toObject());
        if (!e.docId.isEmpty())
            m_entries.insert(e.docId, e);
    }
    return true;
}

bool DocManager::save(QString &errorMessage) const
{
    if (m_configPath.isEmpty()) {
        errorMessage = QStringLiteral("文档配置路径为空");
        return false;
    }
    QJsonArray docsArr;
    QList<DocEntry> sorted = m_entries.values();
    std::sort(sorted.begin(), sorted.end(), [](const DocEntry &a, const DocEntry &b) {
        if (a.sortOrder != b.sortOrder) return a.sortOrder > b.sortOrder;
        return a.title < b.title;
    });
    for (const DocEntry &e : sorted)
        docsArr.append(e.toJson());

    QJsonObject root;
    root.insert(QStringLiteral("docsDir"), m_docsDir);
    root.insert(QStringLiteral("docs"), docsArr);

    QFile file(m_configPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        errorMessage = QStringLiteral("无法写入文档配置文件: %1").arg(m_configPath);
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

void DocManager::setDocsDir(const QString &dirPath) { m_docsDir = dirPath; }
QString DocManager::docsDir() const { return m_docsDir; }

QList<DocEntry> DocManager::entries() const { return m_entries.values(); }
DocEntry DocManager::entry(const QString &docId) const { return m_entries.value(docId); }
bool DocManager::hasEntry(const QString &docId) const { return m_entries.contains(docId); }

bool DocManager::upsertEntry(const DocEntry &entry, QString &errorMessage)
{
    if (entry.docId.isEmpty()) {
        errorMessage = QStringLiteral("文档 ID 不能为空");
        return false;
    }
    DocEntry e = entry;
    // 自动计算 sha256
    if (!e.fileName.isEmpty()) {
        const QString absPath = docAbsolutePath(e.docId, e.fileName);
        if (QFileInfo::exists(absPath)) {
            e.sha256 = computeFileSha256(absPath);
        }
    }
    m_entries.insert(e.docId, e);
    return true;
}

bool DocManager::removeEntry(const QString &docId)
{
    return m_entries.remove(docId) > 0;
}

QString DocManager::docAbsolutePath(const QString &docId, const QString &fileName) const
{
    return QDir(m_docsDir).absoluteFilePath(docId + QStringLiteral("/") + fileName);
}

QJsonArray DocManager::buildCatalog(const QString &baseDownloadUrl) const
{
    QJsonArray arr;
    QList<DocEntry> sorted = m_entries.values();
    std::sort(sorted.begin(), sorted.end(), [](const DocEntry &a, const DocEntry &b) {
        if (a.sortOrder != b.sortOrder) return a.sortOrder > b.sortOrder;
        return a.title < b.title;
    });
    QString base = baseDownloadUrl;
    if (base.endsWith('/')) base.chop(1);

    for (const DocEntry &e : sorted) {
        QJsonObject obj = e.toJson();
        if (!e.fileName.isEmpty())
            obj.insert(QStringLiteral("downloadUrl"),
                       QStringLiteral("%1/docs/download/%2/%3").arg(base, e.docId, e.fileName));
        arr.append(obj);
    }
    return arr;
}

QString DocManager::computeFileSha256(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) hash.addData(file.read(65536));
    return QString::fromLatin1(hash.result().toHex());
}
