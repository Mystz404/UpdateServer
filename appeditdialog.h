#ifndef APPEDITDIALOG_H
#define APPEDITDIALOG_H

#include "appupdatemanager.h"

#include <QDialog>

class QLineEdit;
class QComboBox;
class QCheckBox;
class QLabel;
class QPlainTextEdit;

/**
 * @brief 应用条目编辑对话框。
 *
 * 用于新增或编辑 AppUpdateEntry，包含：
 * - 基本信息（ID / 名称 / 版本）
 * - 包文件选择与 SHA256 自动计算
 * - 包类型 / 安装模式 / ZIP 策略
 *
 * 使用方式：
 *   AppEditDialog dlg("新增应用", entry, packagesDir, this);
 *   if (dlg.exec() == QDialog::Accepted) {
 *       entry = dlg.result();
 *   }
 */
class AppEditDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AppEditDialog(const QString &title,
                          const AppUpdateEntry &entry,
                          const QString &packagesDir,
                          QWidget *parent = nullptr);

    /** 返回编辑后的完整条目。 */
    AppUpdateEntry result() const;

private:
    void buildUi(const AppUpdateEntry &entry);
    void onBrowsePackage();
    void onBrowseFullPkg();
    void onBrowseDepsDir();
    bool validate();

private:
    QString m_packagesDir;

    QLineEdit *m_edId       = nullptr;
    QLineEdit *m_edName     = nullptr;
    QLineEdit *m_edVersion  = nullptr;
    QLineEdit *m_edPackage  = nullptr;
    QComboBox *m_cbType     = nullptr;
    QLineEdit *m_edSubDir   = nullptr;
    QCheckBox *m_ckAllowMulti = nullptr;
    QCheckBox *m_ckZipExe   = nullptr;
    QCheckBox *m_ckRequiresLogin = nullptr;
    QLabel    *m_lblSha256  = nullptr;
    QPlainTextEdit *m_edRequiredFiles = nullptr;
    QLineEdit *m_edFullPkg  = nullptr;
    QLineEdit *m_edDepsDir  = nullptr;
    QPlainTextEdit *m_edChangeLog = nullptr;
};

#endif // APPEDITDIALOG_H
