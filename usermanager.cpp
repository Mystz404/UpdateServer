#include "usermanager.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

// ---------------------------------------------------------------------------
// 构造
// ---------------------------------------------------------------------------

UserManager::UserManager(QObject *parent) : QObject(parent) {}

// ---------------------------------------------------------------------------
// 加载 / 保存
// ---------------------------------------------------------------------------

bool UserManager::load(const QString &configPath, QString &errorMessage)
{
    m_configPath = configPath;
    const QString absPath = QFileInfo(configPath).absoluteFilePath();

    QFile file(absPath);
    if (!file.exists()) {
        // 配置文件不存在时视为首次使用，内置一个默认管理员账户
        User admin;
        admin.id       = QUuid::createUuid().toString(QUuid::WithoutBraces);
        admin.username = QStringLiteral("admin");
        admin.password = QStringLiteral("admin123");
        admin.role     = QStringLiteral("admin");
        m_users.insert(admin.id, admin);
        return save(errorMessage);
    }

    if (!file.open(QIODevice::ReadOnly)) {
        errorMessage = QStringLiteral("无法读取用户配置文件：%1").arg(absPath);
        return false;
    }

    const QByteArray raw = file.readAll();
    file.close();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        errorMessage = QStringLiteral("用户配置 JSON 格式错误：%1").arg(parseError.errorString());
        return false;
    }

    m_users.clear();
    for (const QJsonValue &val : doc.array()) {
        const QJsonObject obj = val.toObject();
        User u;
        u.id       = obj.value(QStringLiteral("id")).toString().trimmed();
        u.username = obj.value(QStringLiteral("username")).toString().trimmed();
        u.password = obj.value(QStringLiteral("password")).toString();
        u.role     = obj.value(QStringLiteral("role")).toString().trimmed();
        for (const QJsonValue &v : obj.value(QStringLiteral("allowedApps")).toArray()) {
            const QString s = v.toString().trimmed();
            if (!s.isEmpty()) u.allowedApps.append(s);
        }
        if (!u.id.isEmpty() && !u.username.isEmpty()) {
            m_users.insert(u.id, u);
        }
    }
    return true;
}

bool UserManager::save(QString &errorMessage) const
{
    if (m_configPath.isEmpty()) {
        errorMessage = QStringLiteral("用户配置路径为空，无法保存");
        return false;
    }

    QJsonArray arr;
    for (const User &u : m_users) {
        QJsonObject obj;
        obj.insert(QStringLiteral("id"),          u.id);
        obj.insert(QStringLiteral("username"),    u.username);
        obj.insert(QStringLiteral("password"),    u.password);
        obj.insert(QStringLiteral("role"),        u.role);
        QJsonArray appsArr;
        for (const QString &a : u.allowedApps) appsArr.append(a);
        obj.insert(QStringLiteral("allowedApps"),  appsArr);
        arr.append(obj);
    }

    const QString absPath = QFileInfo(m_configPath).absoluteFilePath();
    QFile file(absPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        errorMessage = QStringLiteral("无法写入用户配置文件：%1").arg(absPath);
        return false;
    }
    file.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

// ---------------------------------------------------------------------------
// 查询
// ---------------------------------------------------------------------------

QList<User> UserManager::users() const
{
    return m_users.values();
}

User UserManager::user(const QString &id) const
{
    return m_users.value(id);
}

bool UserManager::hasUser(const QString &id) const
{
    return m_users.contains(id);
}

User UserManager::userByName(const QString &username) const
{
    for (const User &u : m_users) {
        if (u.username.compare(username, Qt::CaseInsensitive) == 0) {
            return u;
        }
    }
    return {};
}

bool UserManager::hasUserByName(const QString &username) const
{
    return !userByName(username).id.isEmpty();
}

// ---------------------------------------------------------------------------
// CRUD
// ---------------------------------------------------------------------------

bool UserManager::addUser(const User &user, QString &errorMessage)
{
    if (user.username.trimmed().isEmpty()) {
        errorMessage = QStringLiteral("用户名不能为空");
        return false;
    }
    if (hasUserByName(user.username)) {
        errorMessage = QStringLiteral("用户名「%1」已存在").arg(user.username);
        return false;
    }
    User u = user;
    if (u.id.isEmpty()) {
        u.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    if (u.role.isEmpty()) u.role = QStringLiteral("regular");
    m_users.insert(u.id, u);
    return true;
}

bool UserManager::updateUser(const User &user, QString &errorMessage)
{
    if (!m_users.contains(user.id)) {
        errorMessage = QStringLiteral("用户 ID 不存在：%1").arg(user.id);
        return false;
    }
    // 检查用户名唯一性（排除自身）
    const User existing = userByName(user.username);
    if (!existing.id.isEmpty() && existing.id != user.id) {
        errorMessage = QStringLiteral("用户名「%1」已被占用").arg(user.username);
        return false;
    }
    m_users.insert(user.id, user);
    return true;
}

bool UserManager::removeUser(const QString &id)
{
    return m_users.remove(id) > 0;
}

// ---------------------------------------------------------------------------
// 身份验证
// ---------------------------------------------------------------------------

User UserManager::authenticate(const QString &username, const QString &password) const
{
    const User u = userByName(username);
    if (u.id.isEmpty()) return {};
    if (u.password != password) return {};
    return u;
}

// ---------------------------------------------------------------------------
// (hashPassword 已移除，密码明文存储)
// ---------------------------------------------------------------------------
