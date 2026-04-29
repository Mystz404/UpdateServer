#ifndef APPUPDATEMANAGER_H
#define APPUPDATEMANAGER_H

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>

/**
 * @brief 历史版本记录。
 */
struct HistoryVersion
{
    QString version;
    QString fileName;
    QString sha256;
    QString changeLog;   ///< 该版本的更新说明
};

/**
 * @brief 单个应用的升级元数据。
 *
 * 对应客户端 OnlineAppInfo 的服务端视角：
 * - latestVersion  ：服务端声明的最新版本号；
 * - packageFileName：升级包在 packages/{appId}/ 目录下的文件名（如 AppA.exe 或 AppA.zip）；
 * - sha256         ：升级包的 SHA256 校验值（十六进制小写）；
 * - packageType    ："exe" 或 "zip"；
 * - subDir         ：客户端安装子目录（相对 appsRoot）。
 */
struct AppUpdateEntry
{
    QString appId;              ///< 应用唯一标识，与客户端 apps.json 的 id 对应
    QString appName;            ///< 应用显示名称
    QString latestVersion;      ///< 最新版本号
    QString packageFileName;    ///< 升级包文件名（位于 packages/{appId}/ 目录）
    QString sha256;             ///< 升级包 SHA256 校验值
    QString packageType;        ///< "exe" 或 "zip"
    QString subDir;             ///< 客户端安装子目录（相对 appsRoot），为空表示安装到根目录
    bool allowMultiInstance = false;       ///< 是否允许客户端多开
    bool zipReplaceExeRecursively = true;  ///< zip 升级时是否递归替换所有 exe
    bool requiresLogin = false;            ///< true = 需要登录后才在目录中可见
    QStringList requiredFiles;    ///< 依赖文件列表（相对客户端 appsRoot），用于升级前完整性检查
    QString fullPackageFileName;  ///< 完整软件包文件名（位于 packages/{appId}/），依赖不完整时供客户端下载
    QString depsDir;              ///< 依赖文件目录（绝对路径），少量缺失时供客户端逐文件下载
    QVector<HistoryVersion> history; ///< 历史版本记录
    QString changeLog;               ///< 当前版本的更新说明
};

/**
 * @brief 服务端应用升级包管理器。
 *
 * 负责：
 * 1) 从持久化配置（server_apps.json）加载 / 保存应用元数据列表；
 * 2) 提供增删改查接口供界面和 HTTP 层使用；
 * 3) 自动计算升级包 SHA256；
 * 4) 生成客户端所需的在线元数据 JSON。
 */
class AppUpdateManager : public QObject
{
    Q_OBJECT

public:
    explicit AppUpdateManager(QObject *parent = nullptr);

    /// 加载持久化配置，configPath 一般为 server_apps.json
    bool load(const QString &configPath, QString &errorMessage);

    /// 保存到持久化配置
    bool save(QString &errorMessage) const;

    /// 设置升级包存放的根目录
    void setPackagesDir(const QString &dirPath);
    QString packagesDir() const;

    /// 设置静态元数据输出目录（updates/）
    void setUpdatesDir(const QString &dirPath);
    QString updatesDir() const;

    /// 设置对外访问基础 URL（如 http://download.bmshub.asia）
    void setPublicBaseUrl(const QString &urlText);
    QString publicBaseUrl() const;

    /// 获取全部应用条目
    QList<AppUpdateEntry> entries() const;

    /// 按 appId 获取
    AppUpdateEntry entry(const QString &appId) const;
    bool hasEntry(const QString &appId) const;

    /// 新增或更新一个条目，同时自动计算 sha256
    bool upsertEntry(const AppUpdateEntry &entry, QString &errorMessage);

    /// 删除一个条目
    bool removeEntry(const QString &appId);

    /// 生成客户端请求的在线元数据 JSON
    QJsonObject buildClientMeta(const QString &appId, const QString &baseDownloadUrl) const;

    /// 将所有应用的元数据写入 updates/ 目录下的静态 JSON 文件
    bool generateAllStaticMeta(const QString &baseDownloadUrl, QString &errorMessage);

    /// 清空 updates/ 目录下所有静态 JSON 文件
    void clearStaticMeta();

    /// 获取升级包的绝对路径（packages/{appId}/{packageFileName}）
    QString packageAbsolutePath(const QString &appId, const QString &packageFileName) const;

    /// 将当前版本归档到历史目录
    bool archiveCurrentVersion(const QString &appId, QString &errorMessage);

    /// 删除某个历史版本记录（直接修改 m_entries，不经过 upsertEntry）
    bool removeHistoryVersion(const QString &appId, int index);

    /// 更新某个历史版本的更新说明
    bool updateHistoryChangeLog(const QString &appId, int index, const QString &changeLog);

    /// 获取应用历史版本列表
    QVector<HistoryVersion> historyVersions(const QString &appId) const;

    /// 构建应用清单 JSON（供客户端获取所有可下载应用列表）
    QJsonArray buildCatalog(const QString &baseDownloadUrl) const;

    /// 构建某个应用的历史版本 JSON
    QJsonObject buildHistoryMeta(const QString &appId, const QString &baseDownloadUrl) const;

    /// 获取某个应用的历史版本存放目录
    QString historyDirForApp(const QString &appId) const;

    /// 计算文件 SHA256
    static QString computeFileSha256(const QString &filePath);

private:
    QString m_configPath;
    QString m_packagesDir;
    QString m_updatesDir;
    QString m_publicBaseUrl;
    QHash<QString, AppUpdateEntry> m_entries;
};

#endif // APPUPDATEMANAGER_H
