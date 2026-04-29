#ifndef DOCEDITDIALOG_H
#define DOCEDITDIALOG_H

#include "docentry.h"

#include <QDialog>

class QLineEdit;
class QPlainTextEdit;
class QSpinBox;
class QLabel;
class QCheckBox;

/**
 * @brief 文档条目编辑对话框
 *
 * 用于新增或编辑 DocEntry，包含：
 * - docId / 标题 / 版本
 * - 分类（多个，逗号分隔）
 * - 描述
 * - 文档文件选择（支持 PDF/Word/Excel/PPT/TXT/Markdown 等，自动计算 SHA256）
 * - 关键词（逗号分隔）
 * - 排序权重
 */
class DocEditDialog : public QDialog
{
    Q_OBJECT
public:
    explicit DocEditDialog(const QString &title,
                           const DocEntry &entry,
                           const QString &docsDir,
                           QWidget *parent = nullptr);

    DocEntry result() const;

private:
    void buildUi(const DocEntry &entry);
    void onBrowseFile();
    bool validate();

    QString m_docsDir;

    QLineEdit    *m_edId          = nullptr;
    QLineEdit    *m_edTitle       = nullptr;
    QLineEdit    *m_edVersion     = nullptr;
    QLineEdit    *m_edCategories  = nullptr;
    QPlainTextEdit *m_edDesc      = nullptr;
    QLineEdit    *m_edFileName    = nullptr;
    QLabel       *m_lblSha256     = nullptr;
    QLineEdit    *m_edKeywords    = nullptr;
    QSpinBox     *m_spinSort      = nullptr;
    QCheckBox    *m_ckRequiresLogin = nullptr;
};

#endif // DOCEDITDIALOG_H
