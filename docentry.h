#ifndef DOCENTRY_H
#define DOCENTRY_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

/**
 * @brief 文档管理条目
 *
 * 每个文档条目代表一份 PDF 文件，具有：
 * - 唯一标识 docId
 * - 显示名称 title
 * - 发布版本 version（用于客户端判断是否需要重新下载）
 * - 多分类标签 categories（客户端据此分组显示）
 * - 简介描述 description
 * - 文件名 fileName（存放于 docs/{docId}/ 目录）
 * - SHA256 校验值
 * - 关键词 keywords（搜索辅助）
 * - 排序权重 sortOrder（越大越靠前）
 */
struct DocEntry
{
    QString     docId;
    QString     title;
    QString     version;
    QStringList categories;   ///< 可多值，如 ["用户手册", "安装指南"]
    QString     description;
    QString     fileName;     ///< PDF 文件名，位于 docs/{docId}/
    QString     sha256;
    QStringList keywords;
    int         sortOrder = 0;
    bool        requiresLogin = false; ///< true = 需要登录后才可查看

    QJsonObject toJson() const
    {
        QJsonObject obj;
        obj.insert(QStringLiteral("docId"),         docId);
        obj.insert(QStringLiteral("title"),          title);
        obj.insert(QStringLiteral("version"),        version);
        QJsonArray catArr;
        for (const QString &c : categories) catArr.append(c);
        obj.insert(QStringLiteral("categories"),     catArr);
        obj.insert(QStringLiteral("description"),    description);
        obj.insert(QStringLiteral("fileName"),       fileName);
        obj.insert(QStringLiteral("sha256"),         sha256);
        QJsonArray kwArr;
        for (const QString &k : keywords) kwArr.append(k);
        obj.insert(QStringLiteral("keywords"),       kwArr);
        obj.insert(QStringLiteral("sortOrder"),      sortOrder);
        obj.insert(QStringLiteral("requiresLogin"),  requiresLogin);
        return obj;
    }

    static DocEntry fromJson(const QJsonObject &obj)
    {
        DocEntry e;
        e.docId       = obj.value(QStringLiteral("docId")).toString().trimmed();
        e.title       = obj.value(QStringLiteral("title")).toString().trimmed();
        e.version     = obj.value(QStringLiteral("version")).toString().trimmed();
        for (const QJsonValue &v : obj.value(QStringLiteral("categories")).toArray())
            e.categories.append(v.toString().trimmed());
        e.description = obj.value(QStringLiteral("description")).toString().trimmed();
        e.fileName    = obj.value(QStringLiteral("fileName")).toString().trimmed();
        e.sha256      = obj.value(QStringLiteral("sha256")).toString().trimmed().toLower();
        for (const QJsonValue &v : obj.value(QStringLiteral("keywords")).toArray())
            e.keywords.append(v.toString().trimmed());
        e.sortOrder     = obj.value(QStringLiteral("sortOrder")).toInt(0);
        e.requiresLogin = obj.value(QStringLiteral("requiresLogin")).toBool(false);
        return e;
    }
};

#endif // DOCENTRY_H
