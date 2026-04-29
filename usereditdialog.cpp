#include "usereditdialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>

// ---------------------------------------------------------------------------
// 构造
// ---------------------------------------------------------------------------

UserEditDialog::UserEditDialog(const QString &title,
                               const User &user,
                               QWidget *parent)
    : QDialog(parent)
    , m_isEdit(!user.id.isEmpty())
    , m_userId(user.id)
{
    setWindowTitle(title);
    setMinimumWidth(400);
    buildUi(user);
}

// ---------------------------------------------------------------------------
// UI 构建
// ---------------------------------------------------------------------------

void UserEditDialog::buildUi(const User &user)
{
    auto *form = new QFormLayout(this);
    form->setSpacing(10);

    m_edUsername = new QLineEdit(user.username, this);
    m_edUsername->setPlaceholderText(QStringLiteral("登录用户名"));
    form->addRow(QStringLiteral("用户名："), m_edUsername);

    m_edPassword = new QLineEdit(this);
    m_edPassword->setEchoMode(QLineEdit::Normal);  // 编辑时显示明文密码
    if (m_isEdit) {
        m_edPassword->setText(user.password);      // 回填当前密码
        m_edPassword->setPlaceholderText(QStringLiteral("登录密码"));
    } else {
        m_edPassword->setPlaceholderText(QStringLiteral("登录密码"));
    }
    form->addRow(QStringLiteral("密码："), m_edPassword);

    m_cbRole = new QComboBox(this);
    m_cbRole->addItem(QStringLiteral("管理员"), QStringLiteral("admin"));
    m_cbRole->addItem(QStringLiteral("普通用户"), QStringLiteral("regular"));
    if (user.role == QStringLiteral("admin")) {
        m_cbRole->setCurrentIndex(0);
    } else {
        m_cbRole->setCurrentIndex(1);
    }
    form->addRow(QStringLiteral("角色："), m_cbRole);

    m_edAllowedApps = new QPlainTextEdit(user.allowedApps.join('\n'), this);
    m_edAllowedApps->setPlaceholderText(QStringLiteral("每行填写一个应用 ID；留空表示允许访问全部应用"));
    m_edAllowedApps->setFixedHeight(100);
    form->addRow(QStringLiteral("允许的应用："), m_edAllowedApps);

    auto *hint = new QLabel(QStringLiteral("（留空=允许所有应用；可填多行 appId）"), this);
    hint->setStyleSheet(QStringLiteral("color: #6b7280; font-size: 11px;"));
    form->addRow(QString(), hint);

    auto *btnBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(btnBox, &QDialogButtonBox::accepted, this, [this]() {
        if (validate()) accept();
    });
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    form->addRow(btnBox);
}

// ---------------------------------------------------------------------------
// 验证
// ---------------------------------------------------------------------------

bool UserEditDialog::validate()
{
    const QString uname = m_edUsername->text().trimmed();
    if (uname.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("验证失败"),
                             QStringLiteral("用户名不能为空。"));
        return false;
    }
    if (!m_isEdit && m_edPassword->text().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("验证失败"),
                             QStringLiteral("新增用户时必须设置密码。"));
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// 结果
// ---------------------------------------------------------------------------

User UserEditDialog::result() const
{
    User u;
    u.id       = m_userId.isEmpty()
                     ? QString()
                     : m_userId;
    u.username = m_edUsername->text().trimmed();
    u.role     = m_cbRole->currentData().toString();

    const QString pwd = m_edPassword->text();
    u.password = pwd;
    const QStringList lines = m_edAllowedApps->toPlainText().split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QString id = line.trimmed();
        if (!id.isEmpty()) u.allowedApps.append(id);
    }
    return u;
}
