#ifndef USEREDITDIALOG_H
#define USEREDITDIALOG_H

#include "usermanager.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;

/**
 * @brief 用户新增 / 编辑对话框。
 *
 * 支持：
 * - 用户名、密码（编辑时留空表示不修改）
 * - 角色选择（管理员 / 普通用户）
 * - 允许访问的应用 ID 列表（多行，每行一个；留空表示允许全部）
 */
class UserEditDialog : public QDialog
{
    Q_OBJECT

public:
    explicit UserEditDialog(const QString &title,
                            const User &user,
                            QWidget *parent = nullptr);

    User result() const;

private:
    void buildUi(const User &user);
    bool validate();

    bool    m_isEdit = false; ///< true = 编辑模式；false = 新增模式
    QString m_userId;         ///< 编辑模式保留原 id

    QLineEdit      *m_edUsername    = nullptr;
    QLineEdit      *m_edPassword    = nullptr;
    QComboBox      *m_cbRole        = nullptr;
    QPlainTextEdit *m_edAllowedApps = nullptr;
};

#endif // USEREDITDIALOG_H
