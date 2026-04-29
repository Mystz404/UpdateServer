#ifndef DOCMANAGER_H
#define DOCMANAGER_H

#include "docentry.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

/**
 * @brief 文档管理器
 *
 * 负责：
 * 1) 从 server_docs.json 加载 / 保存文档条目列表；
 * 2) 提供增删改查接口；
 * 3) 自动计算 SHA256；
 * 4) 提供 HTTP 层所需的目录路径。
 */
class DocManager : public QObject
{
    Q_OBJECT
public:
    explicit DocManager(QObject *parent = nullptr);

    bool load(const QString &configPath, QString &errorMessage);
    bool save(QString &errorMessage) const;

    void setDocsDir(const QString &dirPath);
    QString docsDir() const;

    QList<DocEntry> entries() const;
    DocEntry        entry(const QString &docId) const;
    bool            hasEntry(const QString &docId) const;

    /// 新增或更新文档，同时自动计算 sha256
    bool upsertEntry(const DocEntry &entry, QString &errorMessage);
    bool removeEntry(const QString &docId);

    /// 文档 PDF 的绝对路径
    QString docAbsolutePath(const QString &docId, const QString &fileName) const;

    /// 构建文档清单 JSON（供客户端 /docs/catalog 路由使用）
    QJsonArray buildCatalog(const QString &baseDownloadUrl) const;

    static QString computeFileSha256(const QString &filePath);

private:
    QString m_configPath;
    QString m_docsDir;
    QHash<QString, DocEntry> m_entries;
};

#endif // DOCMANAGER_H
