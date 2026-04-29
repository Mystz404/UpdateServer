#include "appeditdialog.h"
#include "../AppManager/versionutils.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>

static bool isAllZeroVersion(const QString &version)
{
    const QStringList parts = version.split('.', Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        return true;
    }
    for (const QString &p : parts) {
        if (p.toInt() != 0) {
            return false;
        }
    }
    return true;
}

static QString parseVersionFromFileName(const QString &fileName)
{
    // 支持从文件名中提取版本，例如：AppManagerSetup_1.0.3.exe / xxx-v2.1.0.4.zip
    const QRegularExpression re(QStringLiteral("(\\d+(?:\\.\\d+){1,3})"));
    const QRegularExpressionMatch match = re.match(fileName);
    if (!match.hasMatch()) {
        return {};
    }
    return match.captured(1);
}

// ============================================================
// 构造
// ============================================================

AppEditDialog::AppEditDialog(const QString &title,
                             const AppUpdateEntry &entry,
                             const QString &packagesDir,
                             QWidget *parent)
    : QDialog(parent)
    , m_packagesDir(packagesDir)
{
    setWindowTitle(title);
    setMinimumWidth(500);
    buildUi(entry);
}

// ============================================================
// UI 构建
// ============================================================

void AppEditDialog::buildUi(const AppUpdateEntry &entry)
{
    auto *form = new QFormLayout(this);

    // 基本信息
    m_edId      = new QLineEdit(entry.appId, this);
    m_edName    = new QLineEdit(entry.appName, this);
    m_edVersion = new QLineEdit(entry.latestVersion, this);
    m_edVersion->setReadOnly(true);
    m_edVersion->setPlaceholderText(QStringLiteral("选择包文件后自动读取"));

    form->addRow(QStringLiteral("应用 ID："), m_edId);
    form->addRow(QStringLiteral("应用名称："), m_edName);
    form->addRow(QStringLiteral("最新版本："), m_edVersion);

    // 包文件
    m_edPackage = new QLineEdit(entry.packageFileName, this);
    auto *btnBrowse = new QPushButton(QStringLiteral("选择文件..."), this);
    connect(btnBrowse, &QPushButton::clicked, this, &AppEditDialog::onBrowsePackage);

    form->addRow(QStringLiteral("包文件："), m_edPackage);
    form->addRow(QString(), btnBrowse);

    // 类型与子目录
    m_cbType = new QComboBox(this);
    m_cbType->addItems({QStringLiteral("exe"), QStringLiteral("zip")});
    m_cbType->setCurrentText(entry.packageType.isEmpty() ? QStringLiteral("exe") : entry.packageType);

    m_edSubDir = new QLineEdit(entry.subDir, this);
    m_edSubDir->setPlaceholderText(QStringLiteral("客户端安装子目录，如 BCTester，为空表示根目录"));

    m_ckAllowMulti = new QCheckBox(QStringLiteral("允许客户端多开（显示“在新窗口打开”）"), this);
    m_ckAllowMulti->setChecked(entry.allowMultiInstance);

    m_ckZipExe = new QCheckBox(QStringLiteral("ZIP 升级时递归替换目录下所有 EXE"), this);
    m_ckZipExe->setChecked(entry.zipReplaceExeRecursively);

    m_ckRequiresLogin = new QCheckBox(QStringLiteral("需要登录后才在客户端目录中可见"), this);
    m_ckRequiresLogin->setChecked(entry.requiresLogin);

    form->addRow(QStringLiteral("包类型："), m_cbType);
    form->addRow(QStringLiteral("安装子目录："), m_edSubDir);
    form->addRow(QStringLiteral("多开配置："), m_ckAllowMulti);
    form->addRow(QStringLiteral("ZIP 策略："), m_ckZipExe);
    form->addRow(QStringLiteral("访问权限："), m_ckRequiresLogin);

    // 依赖文件列表
    m_edRequiredFiles = new QPlainTextEdit(this);
    m_edRequiredFiles->setMaximumHeight(90);
    m_edRequiredFiles->setPlaceholderText(QStringLiteral("每行一个相对路径，如 BCTester/config.ini"));
    m_edRequiredFiles->setPlainText(entry.requiredFiles.join('\n'));
    form->addRow(QStringLiteral("依赖文件："), m_edRequiredFiles);

    // 完整包文件名
    m_edFullPkg = new QLineEdit(entry.fullPackageFileName, this);
    m_edFullPkg->setPlaceholderText(QStringLiteral("依赖不完整时供下载的完整包，如 BCTester_full.zip"));
    auto *btnBrowseFullPkg = new QPushButton(QStringLiteral("选择完整包..."), this);
    connect(btnBrowseFullPkg, &QPushButton::clicked, this, &AppEditDialog::onBrowseFullPkg);
    form->addRow(QStringLiteral("完整包："), m_edFullPkg);
    form->addRow(QString(), btnBrowseFullPkg);

    // 依赖文件目录
    m_edDepsDir = new QLineEdit(entry.depsDir, this);
    m_edDepsDir->setPlaceholderText(QStringLiteral("依赖文件所在目录（绝对路径），少量缺失时逐文件下载"));
    auto *btnBrowseDepsDir = new QPushButton(QStringLiteral("选择目录..."), this);
    connect(btnBrowseDepsDir, &QPushButton::clicked, this, &AppEditDialog::onBrowseDepsDir);
    form->addRow(QStringLiteral("依赖目录："), m_edDepsDir);
    form->addRow(QString(), btnBrowseDepsDir);

    // 版本更新说明
    m_edChangeLog = new QPlainTextEdit(this);
    m_edChangeLog->setMaximumHeight(80);
    m_edChangeLog->setPlaceholderText(QStringLiteral("该版本的更新说明，支持多行"));
    m_edChangeLog->setPlainText(entry.changeLog);
    form->addRow(QStringLiteral("版本说明："), m_edChangeLog);

    // SHA256
    m_lblSha256 = new QLabel(this);
    m_lblSha256->setWordWrap(true);
    m_lblSha256->setText(entry.sha256.isEmpty()
                             ? QStringLiteral("（选择包文件后自动计算）")
                             : entry.sha256);
    form->addRow(QStringLiteral("SHA256："), m_lblSha256);

    // 按钮
    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    form->addRow(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        if (validate()) {
            accept();
        }
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

// ============================================================
// 选择包文件
// ============================================================

void AppEditDialog::onBrowsePackage()
{
    const QString appId = m_edId->text().trimmed();
    if (appId.isEmpty()) {
        QMessageBox::warning(this,
                             QStringLiteral("提示"),
                             QStringLiteral("请先填写应用 ID，再选择升级包。"));
        m_edId->setFocus();
        return;
    }

    QSettings settings;
    const QString lastDir = settings.value(QStringLiteral("AppEditDialog/lastPackageDir"), m_packagesDir).toString();

    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("选择升级包文件"),
        lastDir,
        QStringLiteral("所有文件 (*.*);;EXE (*.exe);;ZIP (*.zip)"));

    if (path.isEmpty()) {
        return;
    }

    settings.setValue(QStringLiteral("AppEditDialog/lastPackageDir"), QFileInfo(path).absolutePath());
    const QFileInfo fi(path);

    // 复制到 packages/{appId}/ 子目录
    const QString targetDir = QDir(m_packagesDir).absoluteFilePath(appId);
    if (!QDir().mkpath(targetDir)) {
        QMessageBox::warning(this,
                             QStringLiteral("复制失败"),
                             QStringLiteral("无法创建目录：%1").arg(targetDir));
        return;
    }
    const QString destPath = QDir(targetDir).absoluteFilePath(fi.fileName());
    const bool hadOldFile = QFileInfo::exists(destPath);
    // 不在删除前读取旧文件（避免 Windows 文件锁），复用已显示的 SHA
    const QString oldSha256 = hadOldFile ? m_lblSha256->text() : QString();

    // 如果文件不在目标目录下，复制过去（存在同名文件时强制覆盖）
    if (QFileInfo(destPath).absoluteFilePath() != fi.absoluteFilePath()) {
        if (QFileInfo::exists(destPath) && !QFile::remove(destPath)) {
            QMessageBox::warning(this,
                                 QStringLiteral("复制失败"),
                                 QStringLiteral("无法覆盖旧文件：%1").arg(destPath));
            return;
        }
        if (!QFile::copy(path, destPath)) {
            QMessageBox::warning(this,
                                 QStringLiteral("复制失败"),
                                 QStringLiteral("文件复制失败：%1 -> %2").arg(path, destPath));
            return;
        }
    }

    m_edPackage->setText(fi.fileName());
    m_cbType->setCurrentText(fi.suffix().compare(QStringLiteral("zip"), Qt::CaseInsensitive) == 0
                                 ? QStringLiteral("zip")
                                 : QStringLiteral("exe"));

    // 自动计算 SHA256
    const QString sha = AppUpdateManager::computeFileSha256(destPath);
    m_lblSha256->setText(sha);

    if (hadOldFile) {
        if (oldSha256 != sha) {
            QMessageBox::information(
                this,
                QStringLiteral("替换确认"),
                QStringLiteral("升级包已替换为新文件。\n旧 SHA256: %1\n新 SHA256: %2")
                    .arg(oldSha256.left(16) + QStringLiteral("..."),
                         sha.left(16) + QStringLiteral("...")));
        } else {
            QMessageBox::information(
                this,
                QStringLiteral("替换确认"),
                QStringLiteral("已选择包文件，但内容与原文件一致（SHA256 未变化）。"));
        }
    }

    // 自动从 EXE 文件读取版本号
    if (fi.suffix().toLower() == QStringLiteral("exe")) {
        // 先读 EXE 版本资源；若无效（如 0.0.0.0）则回退到文件名解析。
        QString ver = getFileVersion(destPath);
        if (ver.isEmpty() || isAllZeroVersion(ver)) {
            ver = parseVersionFromFileName(fi.fileName());
        }
        if (!ver.isEmpty()) {
            m_edVersion->setText(ver);
        }
    } else if (fi.suffix().toLower() == QStringLiteral("zip")) {
        // ZIP 包时尝试解压后找 EXE 读取版本（暂不自动，用户可手动设置）
    }
}

// ============================================================
// 选择完整包文件
// ============================================================

void AppEditDialog::onBrowseFullPkg()
{
    const QString appId = m_edId->text().trimmed();
    if (appId.isEmpty()) {
        QMessageBox::warning(this,
                             QStringLiteral("提示"),
                             QStringLiteral("请先填写应用 ID，再选择完整包。"));
        m_edId->setFocus();
        return;
    }

    QSettings settings;
    const QString lastDir = settings.value(QStringLiteral("AppEditDialog/lastFullPkgDir"), m_packagesDir).toString();

    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("选择完整软件包"),
        lastDir,
        QStringLiteral("ZIP 文件 (*.zip);;所有文件 (*.*)"));

    if (path.isEmpty()) {
        return;
    }

    settings.setValue(QStringLiteral("AppEditDialog/lastFullPkgDir"), QFileInfo(path).absolutePath());
    const QFileInfo fi(path);

    // 复制到 packages/{appId}/ 子目录
    const QString targetDir = QDir(m_packagesDir).absoluteFilePath(appId);
    if (!QDir().mkpath(targetDir)) {
        QMessageBox::warning(this,
                             QStringLiteral("复制失败"),
                             QStringLiteral("无法创建目录：%1").arg(targetDir));
        return;
    }
    const QString destPath = QDir(targetDir).absoluteFilePath(fi.fileName());

    if (QFileInfo(destPath).absoluteFilePath() != fi.absoluteFilePath()) {
        if (QFileInfo::exists(destPath) && !QFile::remove(destPath)) {
            QMessageBox::warning(this,
                                 QStringLiteral("复制失败"),
                                 QStringLiteral("无法覆盖旧文件：%1").arg(destPath));
            return;
        }
        if (!QFile::copy(path, destPath)) {
            QMessageBox::warning(this,
                                 QStringLiteral("复制失败"),
                                 QStringLiteral("文件复制失败：%1 -> %2").arg(path, destPath));
            return;
        }
    }

    m_edFullPkg->setText(fi.fileName());
}

// ============================================================
// 选择依赖文件目录
// ============================================================

void AppEditDialog::onBrowseDepsDir()
{
    const QString startDir = m_edDepsDir->text().trimmed().isEmpty()
                                 ? m_packagesDir
                                 : m_edDepsDir->text().trimmed();
    const QString dir = QFileDialog::getExistingDirectory(
        this,
        QStringLiteral("选择依赖文件目录"),
        startDir);
    if (!dir.isEmpty()) {
        m_edDepsDir->setText(QDir::toNativeSeparators(dir));
    }
}

// ============================================================
// 校验
// ============================================================

bool AppEditDialog::validate()
{
    if (m_edId->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("校验失败"),
                             QStringLiteral("应用 ID 不能为空。"));
        m_edId->setFocus();
        return false;
    }
    if (m_edPackage->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("校验失败"),
                             QStringLiteral("包文件不能为空。"));
        m_edPackage->setFocus();
        return false;
    }
    if (m_edVersion->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("校验失败"),
                             QStringLiteral("版本号为空，请选择包含版本信息的 EXE 文件。"));
        return false;
    }
    return true;
}

// ============================================================
// 获取结果
// ============================================================

AppUpdateEntry AppEditDialog::result() const
{
    AppUpdateEntry e;
    e.appId              = m_edId->text().trimmed();
    e.appName            = m_edName->text().trimmed();
    e.latestVersion      = m_edVersion->text().trimmed();
    e.packageFileName    = m_edPackage->text().trimmed();
    e.packageType        = m_cbType->currentText();
    e.subDir             = m_edSubDir->text().trimmed();
    e.allowMultiInstance = m_ckAllowMulti->isChecked();
    e.zipReplaceExeRecursively = m_ckZipExe->isChecked();
    e.requiresLogin      = m_ckRequiresLogin->isChecked();
    e.sha256             = m_lblSha256->text();

    // 依赖文件列表
    e.requiredFiles.clear();
    const QStringList lines = m_edRequiredFiles->toPlainText().split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty()) e.requiredFiles.append(trimmed);
    }
    e.fullPackageFileName = m_edFullPkg->text().trimmed();
    e.depsDir = m_edDepsDir->text().trimmed();
    e.changeLog = m_edChangeLog->toPlainText().trimmed();

    if (e.sha256.contains(QStringLiteral("自动计算"))) {
        e.sha256.clear();
    }

    return e;
}
