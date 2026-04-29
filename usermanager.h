#ifndef USERMANAGER_H
#define USERMANAGER_H

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

/**
 * @brief 用户信息。
 */
struct User
{
    QString     id;           ///< UUID（唯一标识）
    QString     username;     ///< 登录名（唯一）
    QString     password;     ///< 明文密码
    QString     role;         ///< "admin" 或 "regular"
    QStringList allowedApps;  ///< 允许访问的 appId 列表；为空表示允许全部
};

/**
 * @brief 用户管理器。
 *
 * 负责从 server_users.json 加载/保存用户列表，提供增删改查及身份验证接口。
 * 密码以 SHA-256 哈希形式存储，禁止明文持久化。
 */
class UserManager : public QObject
{
    Q_OBJECT

public:
    explicit UserManager(QObject *parent = nullptr);

    bool load(const QString &configPath, QString &errorMessage);
    bool save(QString &errorMessage) const;

    QList<User> users() const;
    User        user(const QString &id) const;
    bool        hasUser(const QString &id) const;

    User userByName(const QString &username) const;
    bool hasUserByName(const QString &username) const;

    /// 新增用户（id 必须唯一；username 必须唯一）
    bool addUser(const User &user, QString &errorMessage);

    /// 更新已有用户（按 id 匹配）
    bool updateUser(const User &user, QString &errorMessage);

    /// 删除用户
    bool removeUser(const QString &id);

/// 验证用户名 + 明文密码，成功返回完整 User；失败返回 id 为空的 User
    User authenticate(const QString &username, const QString &password) const;

private:
    QString             m_configPath;
    QHash<QString, User> m_users; ///< id → User
};

#endif // USERMANAGER_H
