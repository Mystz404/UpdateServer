#include "appupdatemanager.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

// ============================================================================
//  构造
// ============================================================================
AppUpdateManager::AppUpdateManager(QObject *parent)
    : QObject(parent)
{
}

// ============================================================================
//  持久化：加载
// ============================================================================
bool AppUpdateManager::load(const QString &configPath, QString &errorMessage)
{
    m_configPath = configPath;

    const QString absConfigPath = QFileInfo(configPath).absoluteFilePath();
    QFile file(absConfigPath);
    if (!file.exists()) {
        // 首次启动，空配置即可
        return true;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        errorMessage = QStringLiteral("无法打开配置文件，绝对路径：%1").arg(absConfigPath);
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();

    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        errorMessage = QStringLiteral("配置文件 JSON 格式错误（%1），绝对路径：%2")
                           .arg(parseError.errorString(), absConfigPath);
        return false;
    }

    const QJsonObject root = doc.object();

    // 读取 packagesDir，默认为配置文件同级 packages/
    m_packagesDir = root.value(QStringLiteral("packagesDir")).toString().trimmed();
    if (m_packagesDir.isEmpty()) {
        m_packagesDir = QFileInfo(configPath).absolutePath() + QStringLiteral("/packages");
    }
    m_publicBaseUrl = root.value(QStringLiteral("publicBaseUrl")).toString().trimmed();

    const QJsonArray appsArray = root.value(QStringLiteral("apps")).toArray();
    m_entries.clear();
    m_entries.reserve(appsArray.size());

    for (const QJsonValue &val : appsArray) {
        const QJsonObject obj = val.toObject();
        AppUpdateEntry e;
        e.appId              = obj.value(QStringLiteral("appId")).toString().trimmed();
        e.appName            = obj.value(QStringLiteral("appName")).toString().trimmed();
        e.latestVersion      = obj.value(QStringLiteral("latestVersion")).toString().trimmed();
        e.packageFileName    = obj.value(QStringLiteral("packageFileName")).toString().trimmed();
        e.sha256             = obj.value(QStringLiteral("sha256")).toString().trimmed().toLower();
        e.packageType        = obj.value(QStringLiteral("packageType")).toString().trimmed().toLower();
        e.subDir             = obj.value(QStringLiteral("subDir")).toString().trimmed();
        e.allowMultiInstance = obj.value(QStringLiteral("allowMultiInstance")).toBool(false);
        e.zipReplaceExeRecursively = obj.value(QStringLiteral("zipReplaceExeRecursively")).toBool(true);
        e.fullPackageFileName = obj.value(QStringLiteral("fullPackageFileName")).toString().trimmed();
        e.depsDir = obj.value(QStringLiteral("depsDir")).toString().trimmed();
        e.requiresLogin = obj.value(QStringLiteral("requiresLogin")).toBool(false);
        e.changeLog = obj.value(QStringLiteral("changeLog")).toString().trimmed();
        const QJsonArray reqArr = obj.value(QStringLiteral("requiredFiles")).toArray();
        for (const QJsonValue &rf : reqArr) {
            const QString f = rf.toString().trimmed();
            if (!f.isEmpty()) e.requiredFiles.append(f);
        }

        // 加载历史版本记录
        const QJsonArray histArr = obj.value(QStringLiteral("history")).toArray();
        for (const QJsonValue &hv : histArr) {
            const QJsonObject ho = hv.toObject();
            HistoryVersion h;
            h.version  = ho.value(QStringLiteral("version")).toString().trimmed();
            h.fileName = ho.value(QStringLiteral("fileName")).toString().trimmed();
            h.sha256   = ho.value(QStringLiteral("sha256")).toString().trimmed().toLower();
            h.changeLog = ho.value(QStringLiteral("changeLog")).toString().trimmed();
            if (!h.version.isEmpty() && !h.fileName.isEmpty()) {
                e.history.append(h);
            }
        }

        if (e.packageType.isEmpty()) {
            e.packageType = QStringLiteral("exe");
        }

        if (!e.appId.isEmpty()) {
            m_entries.insert(e.appId, e);
        }
    }

    return true;
}

// ============================================================================
//  持久化：保存
// ============================================================================
bool AppUpdateManager::save(QString &errorMessage) const
{
    if (m_configPath.isEmpty()) {
        errorMessage = QStringLiteral("配置路径为空，无法保存");
        return false;
    }

    QJsonArray appsArray;
    for (auto it = m_entries.constBegin(); it != m_entries.constEnd(); ++it) {
        const AppUpdateEntry &e = it.value();
        QJsonObject obj;
        obj.insert(QStringLiteral("appId"),              e.appId);
        obj.insert(QStringLiteral("appName"),            e.appName);
        obj.insert(QStringLiteral("latestVersion"),      e.latestVersion);
        obj.insert(QStringLiteral("packageFileName"),    e.packageFileName);
        obj.insert(QStringLiteral("sha256"),             e.sha256);
        obj.insert(QStringLiteral("packageType"),        e.packageType);
        obj.insert(QStringLiteral("subDir"),              e.subDir);
        obj.insert(QStringLiteral("allowMultiInstance"), e.allowMultiInstance);
        obj.insert(QStringLiteral("zipReplaceExeRecursively"), e.zipReplaceExeRecursively);
        obj.insert(QStringLiteral("fullPackageFileName"), e.fullPackageFileName);
        if (!e.depsDir.isEmpty())
            obj.insert(QStringLiteral("depsDir"), e.depsDir);
        obj.insert(QStringLiteral("requiresLogin"), e.requiresLogin);
        if (!e.changeLog.isEmpty())
            obj.insert(QStringLiteral("changeLog"), e.changeLog);
        QJsonArray reqArr;
        for (const QString &rf : e.requiredFiles) reqArr.append(rf);
        obj.insert(QStringLiteral("requiredFiles"), reqArr);
        // 保存历史版本记录
        if (!e.history.isEmpty()) {
            QJsonArray histArr;
            for (const HistoryVersion &h : e.history) {
                QJsonObject ho;
                ho.insert(QStringLiteral("version"), h.version);
                ho.insert(QStringLiteral("fileName"), h.fileName);
                ho.insert(QStringLiteral("sha256"), h.sha256);
                if (!h.changeLog.isEmpty())
                    ho.insert(QStringLiteral("changeLog"), h.changeLog);
                histArr.append(ho);
            }
            obj.insert(QStringLiteral("history"), histArr);
        }
        appsArray.append(obj);
    }

    QJsonObject root;
    root.insert(QStringLiteral("packagesDir"), m_packagesDir);
    if (!m_publicBaseUrl.isEmpty()) {
        root.insert(QStringLiteral("publicBaseUrl"), m_publicBaseUrl);
    }
    root.insert(QStringLiteral("apps"), appsArray);

    const QString absConfigPath = QFileInfo(m_configPath).absoluteFilePath();
    QFile file(absConfigPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        errorMessage = QStringLiteral("无法写入配置文件，绝对路径：%1").arg(absConfigPath);
        return false;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

// ============================================================================
//  packages 目录
// ============================================================================
void AppUpdateManager::setPackagesDir(const QString &dirPath)
{
    m_packagesDir = dirPath;
}

QString AppUpdateManager::packagesDir() const
{
    return m_packagesDir;
}

void AppUpdateManager::setUpdatesDir(const QString &dirPath)
{
    m_updatesDir = dirPath;
}

QString AppUpdateManager::updatesDir() const
{
    return m_updatesDir;
}

void AppUpdateManager::setPublicBaseUrl(const QString &urlText)
{
    m_publicBaseUrl = urlText.trimmed();
}

QString AppUpdateManager::publicBaseUrl() const
{
    return m_publicBaseUrl;
}

// ============================================================================
//  CRUD
// ============================================================================
QList<AppUpdateEntry> AppUpdateManager::entries() const
{
    return m_entries.values();
}

AppUpdateEntry AppUpdateManager::entry(const QString &appId) const
{
    return m_entries.value(appId);
}

bool AppUpdateManager::hasEntry(const QString &appId) const
{
    return m_entries.contains(appId);
}

bool AppUpdateManager::upsertEntry(const AppUpdateEntry &entry, QString &errorMessage)
{
    if (entry.appId.isEmpty() || entry.latestVersion.isEmpty() || entry.packageFileName.isEmpty()) {
        errorMessage = QStringLiteral("appId / latestVersion / packageFileName 不能为空");
        return false;
    }

    // 自动迁移：若文件不在 packages/{appId}/ 但在 packages/ 根目录，则自动移入子目录
    auto migrateIfNeeded = [this](const QString &appId, const QString &fileName) {
        const QString subDirPath = packageAbsolutePath(appId, fileName);
        const QString flatPath = QDir(m_packagesDir).absoluteFilePath(fileName);
        if (!QFileInfo::exists(flatPath)) {
            return QFileInfo::exists(subDirPath);
        }

        const QString targetDir = QDir(m_packagesDir).absoluteFilePath(appId);
        QDir().mkpath(targetDir);

        // 根目录存在同名新包时，覆盖子目录旧包，避免继续发旧版本。
        if (QFileInfo::exists(subDirPath) && !QFile::remove(subDirPath)) {
            return false;
        }
        return QFile::copy(flatPath, subDirPath);
    };

    AppUpdateEntry e = entry;
    // 兼容旧编辑流程：编辑对话框返回的条目默认不包含 history，
    // 这里保留已有历史版本，避免更新/修改后历史信息被覆盖丢失。
    if (m_entries.contains(e.appId) && e.history.isEmpty()) {
        e.history = m_entries.value(e.appId).history;
    }
    migrateIfNeeded(e.appId, e.packageFileName);

    const QString pkgPath = packageAbsolutePath(e.appId, e.packageFileName);
    if (QFileInfo::exists(pkgPath)) {
        e.sha256 = computeFileSha256(pkgPath);
    } else {
        errorMessage = QStringLiteral("升级包文件不存在，绝对路径：%1")
                           .arg(QFileInfo(pkgPath).absoluteFilePath());
        return false;
    }

    if (e.packageType.isEmpty()) {
        e.packageType = QStringLiteral("exe");
    }

    // 验证完整包文件存在于 packages/{appId}/ 下
    if (!e.fullPackageFileName.isEmpty()) {
        migrateIfNeeded(e.appId, e.fullPackageFileName);
        const QString fullPkgPath = packageAbsolutePath(e.appId, e.fullPackageFileName);
        if (!QFileInfo::exists(fullPkgPath)) {
            errorMessage = QStringLiteral("完整包文件不存在，绝对路径：%1")
                               .arg(QFileInfo(fullPkgPath).absoluteFilePath());
            return false;
        }
    }

    m_entries.insert(e.appId, e);
    return true;
}

bool AppUpdateManager::removeEntry(const QString &appId)
{
    return m_entries.remove(appId) > 0;
}

// ============================================================================
//  生成客户端所需元数据
// ============================================================================
QJsonObject AppUpdateManager::buildClientMeta(const QString &appId,
                                              const QString &baseDownloadUrl) const
{
    QJsonObject result;
    if (!m_entries.contains(appId)) {
        return result;
    }

    const AppUpdateEntry &e = m_entries[appId];

    // 拼接下载 URL：baseDownloadUrl + "/download/" + appId + "/" + packageFileName
    QString downloadUrl = baseDownloadUrl;
    if (!downloadUrl.endsWith('/')) {
        downloadUrl += '/';
    }
    downloadUrl += QStringLiteral("download/") + e.appId + QStringLiteral("/") + e.packageFileName;

    result.insert(QStringLiteral("latestVersion"),      e.latestVersion);
    result.insert(QStringLiteral("downloadUrl"),        downloadUrl);
    result.insert(QStringLiteral("sha256"),             e.sha256);
    result.insert(QStringLiteral("packageType"),        e.packageType);
    result.insert(QStringLiteral("subDir"),             e.subDir);
    result.insert(QStringLiteral("allowMultiInstance"), e.allowMultiInstance);
    result.insert(QStringLiteral("zipReplaceExeRecursively"), e.zipReplaceExeRecursively);

    // 当前版本更新说明
    if (!e.changeLog.isEmpty()) {
        result.insert(QStringLiteral("changeLog"), e.changeLog);
    }

    // 依赖文件列表
    if (!e.requiredFiles.isEmpty()) {
        QJsonArray reqArr;
        for (const QString &rf : e.requiredFiles) reqArr.append(rf);
        result.insert(QStringLiteral("requiredFiles"), reqArr);
    }

    // 完整包下载地址
    if (!e.fullPackageFileName.isEmpty()) {
        QString fullUrl = baseDownloadUrl;
        if (!fullUrl.endsWith('/')) fullUrl += '/';
        fullUrl += QStringLiteral("download/") + e.appId + QStringLiteral("/") + e.fullPackageFileName;
        result.insert(QStringLiteral("fullPackageUrl"), fullUrl);
    }

    // 单文件依赖下载基址
    if (!e.depsDir.isEmpty() && QDir(e.depsDir).exists()) {
        QString depUrl = baseDownloadUrl;
        if (!depUrl.endsWith('/')) depUrl += '/';
        depUrl += QStringLiteral("dep/") + e.appId;
        result.insert(QStringLiteral("depsBaseUrl"), depUrl);
    }

    return result;
}

// ============================================================================
//  静态元数据生成
// ============================================================================

bool AppUpdateManager::generateAllStaticMeta(const QString &baseDownloadUrl,
                                             QString &errorMessage)
{
    if (m_updatesDir.isEmpty()) {
        errorMessage = QStringLiteral("updatesDir 未设置");
        return false;
    }

    QDir dir(m_updatesDir);
    if (!dir.exists() && !QDir().mkpath(m_updatesDir)) {
        errorMessage = QStringLiteral("无法创建 updates 目录：%1").arg(m_updatesDir);
        return false;
    }

    // 先清空旧文件
    clearStaticMeta();

    for (auto it = m_entries.constBegin(); it != m_entries.constEnd(); ++it) {
        const QString &appId = it.key();
        const QJsonObject meta = buildClientMeta(appId, baseDownloadUrl);
        if (meta.isEmpty()) {
            continue;
        }

        const QString filePath = dir.absoluteFilePath(appId + QStringLiteral(".json"));
        QFile file(filePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            errorMessage = QStringLiteral("无法写入静态元数据文件：%1").arg(filePath);
            return false;
        }
        file.write(QJsonDocument(meta).toJson(QJsonDocument::Indented));
        file.close();
    }

    return true;
}

void AppUpdateManager::clearStaticMeta()
{
    if (m_updatesDir.isEmpty()) {
        return;
    }
    QDir dir(m_updatesDir);
    if (!dir.exists()) {
        return;
    }
    const QStringList jsonFiles = dir.entryList({QStringLiteral("*.json")}, QDir::Files);
    for (const QString &f : jsonFiles) {
        dir.remove(f);
    }
}

// ============================================================================
//  历史版本管理
// ============================================================================

QString AppUpdateManager::historyDirForApp(const QString &appId) const
{
    return QDir(m_packagesDir).absoluteFilePath(QStringLiteral("history/") + appId);
}

bool AppUpdateManager::archiveCurrentVersion(const QString &appId, QString &errorMessage)
{
    if (!m_entries.contains(appId)) {
        errorMessage = QStringLiteral("应用 %1 不存在").arg(appId);
        return false;
    }

    const AppUpdateEntry &current = m_entries[appId];
    const QString srcPath = packageAbsolutePath(appId, current.packageFileName);
    if (!QFileInfo::exists(srcPath)) {
        return true; // 源文件不存在，跳过归档
    }

    // 构建历史文件名：BaseName_Version.ext
    QFileInfo fi(current.packageFileName);
    QString histFileName = fi.completeBaseName() + QStringLiteral("_")
                         + current.latestVersion + QStringLiteral(".") + fi.suffix();

    QString histDir = historyDirForApp(appId);
    QDir().mkpath(histDir);

    QString destPath = QDir(histDir).absoluteFilePath(histFileName);
    if (QFileInfo::exists(destPath)) {
        return true; // 已存在相同历史文件
    }

    if (!QFile::copy(srcPath, destPath)) {
        errorMessage = QStringLiteral("归档历史版本失败: %1 → %2")
                           .arg(QDir::toNativeSeparators(srcPath),
                                QDir::toNativeSeparators(destPath));
        return false;
    }

    // 添加到历史记录
    HistoryVersion hv;
    hv.version  = current.latestVersion;
    hv.fileName = histFileName;
    hv.sha256   = computeFileSha256(destPath);
    hv.changeLog = current.changeLog;
    m_entries[appId].history.append(hv);

    return true;
}

QVector<HistoryVersion> AppUpdateManager::historyVersions(const QString &appId) const
{
    if (m_entries.contains(appId)) {
        return m_entries[appId].history;
    }
    return {};
}

bool AppUpdateManager::removeHistoryVersion(const QString &appId, int index)
{
    if (!m_entries.contains(appId)) return false;
    auto &hist = m_entries[appId].history;
    if (index < 0 || index >= hist.size()) return false;
    hist.remove(index);
    return true;
}

bool AppUpdateManager::updateHistoryChangeLog(const QString &appId, int index, const QString &changeLog)
{
    if (!m_entries.contains(appId)) return false;
    auto &hist = m_entries[appId].history;
    if (index < 0 || index >= hist.size()) return false;
    hist[index].changeLog = changeLog;
    return true;
}

QJsonArray AppUpdateManager::buildCatalog(const QString &baseDownloadUrl) const
{
    QJsonArray catalog;
    for (auto it = m_entries.constBegin(); it != m_entries.constEnd(); ++it) {
        const AppUpdateEntry &e = it.value();
        QJsonObject item;
        item.insert(QStringLiteral("appId"), e.appId);
        item.insert(QStringLiteral("appName"), e.appName);
        item.insert(QStringLiteral("latestVersion"), e.latestVersion);
        item.insert(QStringLiteral("packageType"), e.packageType);
        item.insert(QStringLiteral("packageFileName"), e.packageFileName);
        item.insert(QStringLiteral("allowMultiInstance"), e.allowMultiInstance);
        item.insert(QStringLiteral("requiresLogin"), e.requiresLogin);

        QString dlUrl = baseDownloadUrl;
        if (!dlUrl.endsWith('/')) dlUrl += '/';
        dlUrl += QStringLiteral("download/") + e.appId + QStringLiteral("/") + e.packageFileName;
        item.insert(QStringLiteral("downloadUrl"), dlUrl);

        QString metaUrl = baseDownloadUrl;
        if (!metaUrl.endsWith('/')) metaUrl += '/';
        metaUrl += QStringLiteral("updates/") + e.appId + QStringLiteral(".json");
        item.insert(QStringLiteral("updateMetaUrl"), metaUrl);

        if (!e.subDir.isEmpty()) {
            item.insert(QStringLiteral("subDir"), e.subDir);
        }
        if (!e.changeLog.isEmpty()) {
            item.insert(QStringLiteral("changeLog"), e.changeLog);
        }

        catalog.append(item);
    }
    return catalog;
}

QJsonObject AppUpdateManager::buildHistoryMeta(const QString &appId,
                                                const QString &baseDownloadUrl) const
{
    QJsonObject result;
    if (!m_entries.contains(appId)) {
        return result;
    }

    const AppUpdateEntry &e = m_entries[appId];
    result.insert(QStringLiteral("appId"), e.appId);
    result.insert(QStringLiteral("appName"), e.appName);
    result.insert(QStringLiteral("currentVersion"), e.latestVersion);

    QJsonArray versions;
    for (const HistoryVersion &hv : e.history) {
        QJsonObject vo;
        vo.insert(QStringLiteral("version"), hv.version);
        vo.insert(QStringLiteral("fileName"), hv.fileName);
        vo.insert(QStringLiteral("sha256"), hv.sha256);
        if (!hv.changeLog.isEmpty())
            vo.insert(QStringLiteral("changeLog"), hv.changeLog);

        QString dlUrl = baseDownloadUrl;
        if (!dlUrl.endsWith('/')) dlUrl += '/';
        dlUrl += QStringLiteral("download/history/") + appId + QStringLiteral("/") + hv.fileName;
        vo.insert(QStringLiteral("downloadUrl"), dlUrl);

        versions.append(vo);
    }
    result.insert(QStringLiteral("versions"), versions);

    return result;
}

// ============================================================================
//  工具函数
// ============================================================================
QString AppUpdateManager::packageAbsolutePath(const QString &appId, const QString &packageFileName) const
{
    QDir dir(m_packagesDir);
    return dir.absoluteFilePath(appId + QStringLiteral("/") + packageFileName);
}

QString AppUpdateManager::computeFileSha256(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    // 64KB 分块读取，兼顾大文件内存使用
    while (!file.atEnd()) {
        hash.addData(file.read(65536));
    }
    file.close();
    return QString::fromLatin1(hash.result().toHex()).toLower();
}
