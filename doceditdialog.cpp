#include "doceditdialog.h"
#include "docmanager.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

DocEditDialog::DocEditDialog(const QString &title,
                             const DocEntry &entry,
                             const QString &docsDir,
                             QWidget *parent)
    : QDialog(parent)
    , m_docsDir(docsDir)
{
    setWindowTitle(title);
    setMinimumWidth(520);
    buildUi(entry);
}

void DocEditDialog::buildUi(const DocEntry &entry)
{
    auto *form = new QFormLayout;
    form->setSpacing(8);

    m_edId = new QLineEdit(entry.docId, this);
    m_edId->setPlaceholderText(QStringLiteral("英文唯一标识，如 bcmu_user_guide"));
    form->addRow(QStringLiteral("文档 ID *"), m_edId);

    m_edTitle = new QLineEdit(entry.title, this);
    m_edTitle->setPlaceholderText(QStringLiteral("显示名称，如 BCMU 用户手册"));
    form->addRow(QStringLiteral("标题 *"), m_edTitle);

    m_edVersion = new QLineEdit(entry.version, this);
    m_edVersion->setPlaceholderText(QStringLiteral("如 1.0.0"));
    form->addRow(QStringLiteral("版本 *"), m_edVersion);

    m_edCategories = new QLineEdit(entry.categories.join(QStringLiteral(", ")), this);
    m_edCategories->setPlaceholderText(QStringLiteral("逗号分隔，如 用户手册, 安装指南"));
    form->addRow(QStringLiteral("分类"), m_edCategories);

    m_edDesc = new QPlainTextEdit(entry.description, this);
    m_edDesc->setPlaceholderText(QStringLiteral("文档简介（可选）"));
    m_edDesc->setFixedHeight(72);
    form->addRow(QStringLiteral("描述"), m_edDesc);

    // PDF 文件行
    auto *fileWidget = new QWidget(this);
    auto *fileLayout = new QHBoxLayout(fileWidget);
    fileLayout->setContentsMargins(0, 0, 0, 0);
    fileLayout->setSpacing(6);
    m_edFileName = new QLineEdit(entry.fileName, fileWidget);
    m_edFileName->setPlaceholderText(QStringLiteral("文件名"));
    auto *btnBrowse = new QPushButton(QStringLiteral("浏览…"), fileWidget);
    btnBrowse->setFixedWidth(64);
    fileLayout->addWidget(m_edFileName);
    fileLayout->addWidget(btnBrowse);
    form->addRow(QStringLiteral("文档文件 *"), fileWidget);

    m_lblSha256 = new QLabel(entry.sha256.isEmpty() ? QStringLiteral("（自动计算）") : entry.sha256, this);
    m_lblSha256->setWordWrap(true);
    m_lblSha256->setStyleSheet(QStringLiteral("color: gray; font-size: 11px;"));
    form->addRow(QStringLiteral("SHA256"), m_lblSha256);

    m_edKeywords = new QLineEdit(entry.keywords.join(QStringLiteral(", ")), this);
    m_edKeywords->setPlaceholderText(QStringLiteral("逗号分隔，如 说明书, 操作手册"));
    form->addRow(QStringLiteral("关键词"), m_edKeywords);

    m_spinSort = new QSpinBox(this);
    m_spinSort->setRange(-9999, 9999);
    m_spinSort->setValue(entry.sortOrder);
    m_spinSort->setToolTip(QStringLiteral("数值越大越靠前显示"));
    form->addRow(QStringLiteral("排序权重"), m_spinSort);

    m_ckRequiresLogin = new QCheckBox(QStringLiteral("需要登录后才在客户端中可见"), this);
    m_ckRequiresLogin->setChecked(entry.requiresLogin);
    form->addRow(QStringLiteral("访问权限："), m_ckRequiresLogin);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确定"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);
    root->addLayout(form);
    root->addWidget(buttons);

    connect(btnBrowse, &QPushButton::clicked, this, &DocEditDialog::onBrowseFile);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        if (validate()) accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void DocEditDialog::onBrowseFile()
{
    // docId 必须先填写，才能确定目标目录
    const QString id = m_edId->text().trimmed();
    if (id.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("请先填写文档 ID"),
            QStringLiteral("请先填写 [文档 ID] 字段，\n文件将被自动复制到对应的文档目录。"));
        m_edId->setFocus();
        return;
    }

    // 打开文件选择对话框，优先使用上次选择的目录
    const QString subDir = QDir(m_docsDir).absoluteFilePath(id);
    QSettings settings;
    const QString savedDir = settings.value(QStringLiteral("DocEditDialog/lastBrowseDir")).toString();
    const QString startDir = !savedDir.isEmpty() && QDir(savedDir).exists()
                                 ? savedDir
                                 : (QDir(subDir).exists() ? subDir : m_docsDir);

    const QString filePath = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("选择文档文件"),
        startDir,
        QStringLiteral(
            "所有文件 (*.*);;"
            "PDF 文件 (*.pdf);;"
            "Word 文件 (*.docx *.doc);;"
            "Excel 文件 (*.xlsx *.xls);;"
            "PowerPoint 文件 (*.pptx *.ppt);;"
            "Markdown 文件 (*.md);;"
            "文本文件 (*.txt);;"
            "图片 (*.png *.jpg *.jpeg *.bmp *.gif)"));

    if (filePath.isEmpty()) return;

    settings.setValue(QStringLiteral("DocEditDialog/lastBrowseDir"), QFileInfo(filePath).absolutePath());

    const QString fileName   = QFileInfo(filePath).fileName();
    const QString targetDir  = QDir(m_docsDir).absoluteFilePath(id);
    const QString targetPath = QDir(targetDir).absoluteFilePath(fileName);

    // 若所选文件不在目标目录，则自动复制
    if (QDir::cleanPath(filePath) != QDir::cleanPath(targetPath)) {
        QDir().mkpath(targetDir);
        if (QFileInfo::exists(targetPath))
            QFile::remove(targetPath);

        if (!QFile::copy(filePath, targetPath)) {
            QMessageBox::warning(this, QStringLiteral("复制失败"),
                QStringLiteral("无法将文件复制到文档目录：\n%1\n\n请手动将文件放置到该目录后再继续。")
                    .arg(QDir::toNativeSeparators(targetPath)));
            // 仍然填写文件名，但 SHA256 从原路径计算
            m_edFileName->setText(fileName);
            m_lblSha256->setText(QStringLiteral("计算中…"));
            const QString sha = DocManager::computeFileSha256(filePath);
            m_lblSha256->setText(sha.isEmpty() ? QStringLiteral("（无法计算）") : sha);
            return;
        }
    }

    m_edFileName->setText(fileName);
    m_lblSha256->setText(QStringLiteral("计算中…"));

    // 从目标路径计算 SHA256（确保与服务端实际存储的文件一致）
    const QString sha = DocManager::computeFileSha256(targetPath);
    m_lblSha256->setText(sha.isEmpty() ? QStringLiteral("（无法计算）") : sha);
}

bool DocEditDialog::validate()
{
    if (m_edId->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("验证失败"), QStringLiteral("文档 ID 不能为空"));
        m_edId->setFocus();
        return false;
    }
    if (m_edTitle->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("验证失败"), QStringLiteral("标题不能为空"));
        m_edTitle->setFocus();
        return false;
    }
    if (m_edVersion->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("验证失败"), QStringLiteral("版本不能为空"));
        m_edVersion->setFocus();
        return false;
    }
    if (m_edFileName->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("验证失败"), QStringLiteral("请选择文档文件"));
        return false;
    }
    return true;
}

DocEntry DocEditDialog::result() const
{
    DocEntry e;
    e.docId   = m_edId->text().trimmed();
    e.title   = m_edTitle->text().trimmed();
    e.version = m_edVersion->text().trimmed();

    for (const QString &c : m_edCategories->text().split(',', Qt::SkipEmptyParts)) {
        const QString t = c.trimmed();
        if (!t.isEmpty()) e.categories.append(t);
    }

    e.description = m_edDesc->toPlainText().trimmed();
    e.fileName    = m_edFileName->text().trimmed();

    // sha256：若标签已含有效 hex 串则沿用，否则留空（upsertEntry 会重算）
    const QString lblText = m_lblSha256->text().trimmed();
    if (lblText.length() == 64 && !lblText.contains(' '))
        e.sha256 = lblText.toLower();

    for (const QString &k : m_edKeywords->text().split(',', Qt::SkipEmptyParts)) {
        const QString t = k.trimmed();
        if (!t.isEmpty()) e.keywords.append(t);
    }

    e.sortOrder     = m_spinSort->value();
    e.requiresLogin = m_ckRequiresLogin->isChecked();
    return e;
}
