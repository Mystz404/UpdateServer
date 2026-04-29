#include "mainwindow.h"
#include "appeditdialog.h"
#include "appupdatemanager.h"
#include "docmanager.h"
#include "docwindow.h"
#include "httpserver.h"
#include "logger.h"
#include "usermanager.h"
#include "usereditdialog.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMenu>
#include <QMessageBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cstdlib>

// ============================================================
// 构造 / 析构
// ============================================================

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_manager = new AppUpdateManager(this);
    m_httpServer = new HttpServer(m_manager, this);

    // 默认 packages 目录为 exe 同级 packages/
    QString pkgDir = QApplication::applicationDirPath() + "/packages";
    QDir().mkpath(pkgDir);
    m_manager->setPackagesDir(pkgDir);

    // 默认 updates 目录为 exe 同级 updates/
    QString updDir = QApplication::applicationDirPath() + "/updates";
    QDir().mkpath(updDir);
    m_manager->setUpdatesDir(updDir);

    // 加载配置（配置文件与 exe 同级）
    QString configPath = QApplication::applicationDirPath() + "/server_apps.json";
    QString err;
    m_manager->load(configPath, err);

    // 初始化文档管理器
    m_docManager = new DocManager(this);
    QString docsDir = QApplication::applicationDirPath() + "/docs";
    QDir().mkpath(docsDir);
    m_docManager->setDocsDir(docsDir);
    QString docConfigPath = QApplication::applicationDirPath() + "/server_docs.json";
    m_docManager->load(docConfigPath, err);
    m_httpServer->setDocManager(m_docManager);

    // 初始化用户管理器
    m_userManager = new UserManager(this);
    QString userConfigPath = QApplication::applicationDirPath() + "/server_users.json";
    m_userManager->load(userConfigPath, err);
    m_httpServer->setUserManager(m_userManager);

    setupUi();
    applyStyleSheet();
    refreshTable();
    refreshUserTable();
    setupTrayIcon();
    setupLocalServer();

    // 隐藏窗口时不退出应用（无头模式需要）
    qApp->setQuitOnLastWindowClosed(false);

    connect(m_httpServer, &HttpServer::requestReceived,
            this, &MainWindow::onRequestReceived);
    connect(m_httpServer, &HttpServer::connectionWarning,
            this, [this](const QString &msg) {
        appendLog(QStringLiteral("[告警] %1").arg(msg));
    });

    // 初始化日志持久化
    Logger *logger = Logger::instance();
    QString logPath = QApplication::applicationDirPath() + "/server.log";
    logger->openLogFile(logPath);

    setWindowTitle(QStringLiteral("升级服务器管理"));
    resize(1040, 680);
}

MainWindow::~MainWindow()
{
    m_httpServer->stop();
}

// ============================================================
// 界面构建
// ============================================================

void MainWindow::setupUi()
{
    QWidget *central = new QWidget(this);
    setCentralWidget(central);

    QVBoxLayout *mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(12);

    // --------------- 标题 ---------------
    QLabel *title = new QLabel(QStringLiteral("升级服务器管理面板"));
    title->setObjectName("titleLabel");
    title->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(title);

    // --------------- 服务器控制区 ---------------
    QGroupBox *serverGroup = new QGroupBox(QStringLiteral("服务器控制"));
    QHBoxLayout *serverLayout = new QHBoxLayout(serverGroup);

    serverLayout->addWidget(new QLabel(QStringLiteral("监听端口：")));
    m_spinPort = new QSpinBox;
    m_spinPort->setRange(1024, 65535);
    m_spinPort->setValue(8080);
    serverLayout->addWidget(m_spinPort);

    m_btnStartStop = new QPushButton(QStringLiteral("启动服务"));
    m_btnStartStop->setFixedWidth(120);
    connect(m_btnStartStop, &QPushButton::clicked, this, &MainWindow::onStartStopServer);
    serverLayout->addWidget(m_btnStartStop);

    m_btnHeadless = new QPushButton(QStringLiteral("切换无头模式"));
    m_btnHeadless->setFixedWidth(120);
    m_btnHeadless->setEnabled(false);
    connect(m_btnHeadless, &QPushButton::clicked, this, &MainWindow::onToggleHeadless);
    serverLayout->addWidget(m_btnHeadless);

    auto *btnTerminate = new QPushButton(QStringLiteral("结束进程"));
    btnTerminate->setFixedWidth(100);
    btnTerminate->setStyleSheet(QStringLiteral("QPushButton{color:#fff;background:#d32f2f;border-radius:4px;padding:4px 8px;}"
                                               "QPushButton:hover{background:#b71c1c;}"));
    connect(btnTerminate, &QPushButton::clicked, this, &MainWindow::onTerminateProcess);
    serverLayout->addWidget(btnTerminate);

    m_lblStatus = new QLabel(QStringLiteral("● 已停止"));
    m_lblStatus->setObjectName("statusStopped");
    serverLayout->addWidget(m_lblStatus);

    m_lblBaseUrl = new QLabel;
    m_lblBaseUrl->setTextInteractionFlags(Qt::TextSelectableByMouse);
    serverLayout->addWidget(m_lblBaseUrl);

    serverLayout->addStretch();
    mainLayout->addWidget(serverGroup);

    // --------------- 应用列表区 ---------------
    QWidget *appPage = new QWidget;
    QVBoxLayout *appLayout = new QVBoxLayout(appPage);
    appLayout->setContentsMargins(4, 4, 4, 4);
    appLayout->setSpacing(6);

    // 按钮栏
    QHBoxLayout *btnBar = new QHBoxLayout;
    m_btnAdd    = new QPushButton(QStringLiteral("新增应用"));
    m_btnEdit   = new QPushButton(QStringLiteral("编辑"));
    m_btnRemove = new QPushButton(QStringLiteral("删除"));
    m_btnHistory= new QPushButton(QStringLiteral("历史版本"));
    m_btnRefresh= new QPushButton(QStringLiteral("刷新"));
    connect(m_btnAdd,    &QPushButton::clicked, this, &MainWindow::onAddApp);
    connect(m_btnEdit,   &QPushButton::clicked, this, &MainWindow::onEditApp);
    connect(m_btnRemove, &QPushButton::clicked, this, &MainWindow::onRemoveApp);
    connect(m_btnHistory,&QPushButton::clicked, this, &MainWindow::onManageHistory);
    connect(m_btnRefresh,&QPushButton::clicked, this, &MainWindow::onRefreshTable);

    btnBar->addWidget(m_btnAdd);
    btnBar->addWidget(m_btnEdit);
    btnBar->addWidget(m_btnRemove);
    btnBar->addWidget(m_btnHistory);
    btnBar->addWidget(m_btnRefresh);
    btnBar->addStretch();
    appLayout->addLayout(btnBar);

    // 表格
    m_table = new QTableWidget(0, 9);
    m_table->setHorizontalHeaderLabels({
        QStringLiteral("应用ID"),
        QStringLiteral("名称"),
        QStringLiteral("最新版本"),
        QStringLiteral("包文件"),
        QStringLiteral("包类型"),
        QStringLiteral("安装子目录"),
        QStringLiteral("允许多开"),
        QStringLiteral("ZIP递归替换EXE"),
        QStringLiteral("SHA256")
    });
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setDefaultSectionSize(28);
    appLayout->addWidget(m_table);

    // --------------- 文档管理区 ---------------
    m_docWindow = new DocWindow(m_docManager, this);

    // --------------- 用户管理区 ---------------
    QWidget *userPage = new QWidget;
    QVBoxLayout *userLayout = new QVBoxLayout(userPage);
    userLayout->setContentsMargins(4, 4, 4, 4);
    userLayout->setSpacing(6);

    QHBoxLayout *userBtnBar = new QHBoxLayout;
    m_btnUserAdd  = new QPushButton(QStringLiteral("新增用户"));
    m_btnUserEdit = new QPushButton(QStringLiteral("编辑"));
    m_btnUserDel  = new QPushButton(QStringLiteral("删除"));
    auto *btnUserRefresh = new QPushButton(QStringLiteral("刷新"));
    connect(m_btnUserAdd,  &QPushButton::clicked, this, &MainWindow::onAddUser);
    connect(m_btnUserEdit, &QPushButton::clicked, this, &MainWindow::onEditUser);
    connect(m_btnUserDel,  &QPushButton::clicked, this, &MainWindow::onRemoveUser);
    connect(btnUserRefresh,&QPushButton::clicked, this, &MainWindow::onRefreshUserTable);
    userBtnBar->addWidget(m_btnUserAdd);
    userBtnBar->addWidget(m_btnUserEdit);
    userBtnBar->addWidget(m_btnUserDel);
    userBtnBar->addWidget(btnUserRefresh);
    userBtnBar->addStretch();
    userLayout->addLayout(userBtnBar);

    m_userTable = new QTableWidget(0, 3);
    m_userTable->setHorizontalHeaderLabels({
        QStringLiteral("用户名"),
        QStringLiteral("角色"),
        QStringLiteral("允许应用（空=全部）")
    });
    m_userTable->horizontalHeader()->setStretchLastSection(true);
    m_userTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_userTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_userTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_userTable->setAlternatingRowColors(true);
    m_userTable->verticalHeader()->setDefaultSectionSize(28);
    userLayout->addWidget(m_userTable);

    // --------------- 选项卡 ---------------
    QTabWidget *tabWidget = new QTabWidget(central);
    tabWidget->addTab(appPage,      QStringLiteral("应用管理"));
    tabWidget->addTab(m_docWindow,  QStringLiteral("文档管理"));
    tabWidget->addTab(userPage,     QStringLiteral("用户管理"));

    // --------------- 日志区 ---------------
    QGroupBox *logGroup = new QGroupBox(QStringLiteral("请求日志"));
    QVBoxLayout *logLayout = new QVBoxLayout(logGroup);
    m_logView = new QPlainTextEdit;
    m_logView->setReadOnly(true);
    m_logView->setMaximumBlockCount(500);
    logLayout->addWidget(m_logView);

    // 选项卡区与日志区使用可拖拽分割，默认优先展示选项卡区。
    QSplitter *splitter = new QSplitter(Qt::Vertical, central);
    splitter->addWidget(tabWidget);
    splitter->addWidget(logGroup);
    splitter->setChildrenCollapsible(false);
    splitter->setStretchFactor(0, 9);
    splitter->setStretchFactor(1, 2);

    tabWidget->setMinimumHeight(320);
    logGroup->setMinimumHeight(140);

    mainLayout->addWidget(splitter, 1);
}

void MainWindow::applyStyleSheet()
{
    setStyleSheet(R"(
        QMainWindow { background: #f5f5f5; }
        #titleLabel {
            font-size: 20px;
            font-weight: bold;
            color: #1a1a2e;
            padding: 8px;
        }
        QGroupBox {
            font-weight: bold;
            border: 1px solid #d0d0d0;
            border-radius: 6px;
            margin-top: 10px;
            padding-top: 18px;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 12px;
            padding: 0 6px;
        }
        QPushButton {
            background-color: #2563eb;
            color: white;
            border: none;
            border-radius: 4px;
            padding: 6px 16px;
            font-size: 13px;
        }
        QPushButton:hover { background-color: #1d4ed8; }
        QPushButton:pressed { background-color: #1e40af; }
        QPushButton:disabled { background-color: #94a3b8; }
        QTableWidget {
            background: white;
            alternate-background-color: #f8fafc;
            gridline-color: #e2e8f0;
            border: 1px solid #d0d0d0;
            border-radius: 4px;
        }
        QHeaderView::section {
            background: #e2e8f0;
            padding: 4px 8px;
            border: none;
            font-weight: bold;
        }
        QPlainTextEdit {
            background: #1e293b;
            color: #a3e635;
            font-family: Consolas, "Courier New", monospace;
            font-size: 12px;
            border: 1px solid #334155;
            border-radius: 4px;
        }
        QSpinBox {
            padding: 4px;
            border: 1px solid #cbd5e1;
            border-radius: 4px;
        }
        #statusStopped { color: #ef4444; font-weight: bold; }
        #statusRunning { color: #22c55e; font-weight: bold; }
    )");
}

// ============================================================
// 表格刷新
// ============================================================

void MainWindow::refreshTable()
{
    const auto &entries = m_manager->entries();
    m_table->setRowCount(entries.size());

    for (int i = 0; i < entries.size(); ++i) {
        const auto &e = entries[i];
        m_table->setItem(i, 0, new QTableWidgetItem(e.appId));
        m_table->setItem(i, 1, new QTableWidgetItem(e.appName));
        m_table->setItem(i, 2, new QTableWidgetItem(e.latestVersion));
        m_table->setItem(i, 3, new QTableWidgetItem(e.packageFileName));
        m_table->setItem(i, 4, new QTableWidgetItem(e.packageType));
        m_table->setItem(i, 5, new QTableWidgetItem(e.subDir));
        m_table->setItem(i, 6, new QTableWidgetItem(e.allowMultiInstance ? QStringLiteral("是") : QStringLiteral("否")));
        m_table->setItem(i, 7, new QTableWidgetItem(e.zipReplaceExeRecursively ? QStringLiteral("是") : QStringLiteral("否")));
        // SHA256 过长，只显示前 16 字符
        QString shortSha = e.sha256.left(16) + (e.sha256.length() > 16 ? "..." : "");
        m_table->setItem(i, 8, new QTableWidgetItem(shortSha));
    }

    m_table->resizeColumnsToContents();
}

// ============================================================
// 服务器启停
// ============================================================

void MainWindow::onStartStopServer()
{
    if (m_httpServer->isListening()) {
        m_httpServer->stop();
        m_btnStartStop->setText(QStringLiteral("启动服务"));
        m_lblStatus->setText(QStringLiteral("● 已停止"));
        m_lblStatus->setObjectName("statusStopped");
        m_lblBaseUrl->clear();
        m_spinPort->setEnabled(true);
        m_btnHeadless->setEnabled(false);
        appendLog(QStringLiteral("服务器已停止"));
    } else {
        quint16 port = static_cast<quint16>(m_spinPort->value());
        QString err;
        if (!m_httpServer->start(port, err)) {
            QMessageBox::warning(this, QStringLiteral("启动失败"), err);
            return;
        }

        m_btnStartStop->setText(QStringLiteral("停止服务"));
        m_lblStatus->setText(QStringLiteral("● 运行中"));
        m_lblStatus->setObjectName("statusRunning");
        m_lblBaseUrl->setText(m_httpServer->baseUrl());
        m_spinPort->setEnabled(false);
        m_btnHeadless->setEnabled(true);
        appendLog(QStringLiteral("服务器已启动: %1").arg(m_httpServer->baseUrl()));
        regenerateStaticMeta();
    }
    // 重新应用样式以更新 objectName 选择器
    style()->unpolish(m_lblStatus);
    style()->polish(m_lblStatus);
}

// ============================================================
// 应用 CRUD
// ============================================================

void MainWindow::onAddApp()
{
    AppUpdateEntry entry;
    AppEditDialog dlg(QStringLiteral("新增应用"), entry, m_manager->packagesDir(), this);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }

    entry = dlg.result();
    // 如果同 ID 已存在且版本号变化，归档旧版本
    if (m_manager->hasEntry(entry.appId)) {
        auto oldEntry = m_manager->entry(entry.appId);
        if (oldEntry.latestVersion != entry.latestVersion) {
            QString archiveErr;
            if (m_manager->archiveCurrentVersion(entry.appId, archiveErr)) {
                appendLog(QStringLiteral("已归档历史版本: %1 v%2").arg(entry.appId, oldEntry.latestVersion));
            } else {
                appendLog(QStringLiteral("归档历史版本失败: %1").arg(archiveErr));
            }
        }
    }
    QString err;
    if (!m_manager->upsertEntry(entry, err)) {
        QMessageBox::warning(this, QStringLiteral("新增失败"), err);
        return;
    }
    m_manager->save(err);
    refreshTable();
    appendLog(QStringLiteral("已新增应用: %1").arg(entry.appId));
    regenerateStaticMeta();
}

void MainWindow::onEditApp()
{
    const int row = selectedRow();
    if (row < 0) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请先选中一行。"));
        return;
    }

    AppUpdateEntry entry = m_manager->entries().at(row);
    const AppUpdateEntry oldEntry = entry;

    // Bug1 fix: 在打开编辑对话框前备份旧包文件
    //（对话框的 onBrowsePackage 会立即将新文件复制到 packages/{appId}/ 覆盖旧文件）
    const QString oldPkgPath = m_manager->packageAbsolutePath(oldEntry.appId, oldEntry.packageFileName);
    const QString backupPath = oldPkgPath + QStringLiteral(".archive_backup");
    bool hasBackup = false;
    if (QFileInfo::exists(oldPkgPath)) {
        if (QFileInfo::exists(backupPath)) QFile::remove(backupPath);
        hasBackup = QFile::copy(oldPkgPath, backupPath);
    }

    AppEditDialog dlg(QStringLiteral("编辑应用"), entry, m_manager->packagesDir(), this);
    if (dlg.exec() != QDialog::Accepted) {
        if (hasBackup) QFile::remove(backupPath);
        return;
    }

    entry = dlg.result();
    // dlg.result() 不包含 history，手动恢复已有历史记录
    entry.history = m_manager->entry(oldEntry.appId).history;

    // 如果版本号有变化且有备份，从备份文件归档旧版本
    if (m_manager->hasEntry(entry.appId)
        && oldEntry.latestVersion != entry.latestVersion
        && hasBackup) {
        const QString histDir = m_manager->historyDirForApp(entry.appId);
        QDir().mkpath(histDir);
        const QFileInfo fi(oldEntry.packageFileName);
        const QString histFileName = fi.completeBaseName()
            + QStringLiteral("_") + oldEntry.latestVersion
            + QStringLiteral(".") + fi.suffix();
        const QString histPath = QDir(histDir).absoluteFilePath(histFileName);
        if (!QFileInfo::exists(histPath)) {
            QFile::rename(backupPath, histPath);
            HistoryVersion hv;
            hv.version  = oldEntry.latestVersion;
            hv.fileName = histFileName;
            hv.sha256   = AppUpdateManager::computeFileSha256(histPath);
            hv.changeLog = oldEntry.changeLog;
            entry.history.append(hv);
            appendLog(QStringLiteral("已归档历史版本: %1 v%2").arg(entry.appId, oldEntry.latestVersion));
        }
    }
    // 清理备份文件
    if (QFileInfo::exists(backupPath)) QFile::remove(backupPath);
    QString err;
    if (!m_manager->upsertEntry(entry, err)) {
        QMessageBox::warning(this, QStringLiteral("编辑失败"), err);
        return;
    }
    m_manager->save(err);
    refreshTable();
    appendLog(QStringLiteral("已编辑应用: %1").arg(entry.appId));
    regenerateStaticMeta();
}

void MainWindow::onRemoveApp()
{
    int row = selectedRow();
    if (row < 0) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请先选中一行。"));
        return;
    }

    QString appId = m_manager->entries().at(row).appId;
    int ret = QMessageBox::question(this, QStringLiteral("确认删除"),
        QStringLiteral("确定要删除应用「%1」吗？").arg(appId));
    if (ret != QMessageBox::Yes) return;

    m_manager->removeEntry(appId);
    QString err;
    m_manager->save(err);
    refreshTable();
    appendLog(QStringLiteral("已删除应用: %1").arg(appId));
    regenerateStaticMeta();
}

void MainWindow::onRefreshTable()
{
    QString configPath = QApplication::applicationDirPath() + "/server_apps.json";
    QString err;
    m_manager->load(configPath, err);
    refreshTable();
    appendLog(QStringLiteral("已刷新应用列表"));
    regenerateStaticMeta();
}

// ============================================================
// 日志
// ============================================================

void MainWindow::onRequestReceived(const QString &method, const QString &path, int statusCode)
{
    QString ts = QDateTime::currentDateTime().toString("hh:mm:ss");
    appendLog(QStringLiteral("[%1] %2 %3 → %4").arg(ts, method, path).arg(statusCode));
}

void MainWindow::appendLog(const QString &text)
{
    m_logView->appendPlainText(text);
    Logger::instance()->log(text);
}

int MainWindow::selectedRow() const
{
    auto selected = m_table->selectionModel()->selectedRows();
    if (selected.isEmpty()) return -1;
    return selected.first().row();
}

void MainWindow::regenerateStaticMeta()
{
    if (!m_httpServer->isListening()) {
        return;  // 服务器未启动时无需生成（baseUrl 未知）
    }
    QString err;
    const QString metaBaseUrl = m_httpServer->baseUrl();
    if (m_manager->generateAllStaticMeta(metaBaseUrl, err)) {
        appendLog(QStringLiteral("已重新生成静态元数据文件 (%1 个应用)")
                      .arg(m_manager->entries().size()));
    } else {
        appendLog(QStringLiteral("静态元数据生成失败：%1").arg(err));
    }
}

// ============================================================
// 系统托盘与无头模式
// ============================================================

static const char kLocalServerName[] = "UpdateServerInstance";

void MainWindow::setupTrayIcon()
{
    m_trayIcon = new QSystemTrayIcon(QIcon(QStringLiteral(":/resources/server.png")), this);
    m_trayIcon->setToolTip(QStringLiteral("升级服务器"));

    QMenu *trayMenu = new QMenu(this);
    trayMenu->addAction(QStringLiteral("显示界面"), this, &MainWindow::showFromTray);
    trayMenu->addAction(QStringLiteral("退出"), this, &MainWindow::quitFromTray);
    m_trayIcon->setContextMenu(trayMenu);

    connect(m_trayIcon, &QSystemTrayIcon::activated,
            this, &MainWindow::onTrayActivated);
}

void MainWindow::setupLocalServer()
{
    m_localServer = new QLocalServer(this);
    QLocalServer::removeServer(QLatin1String(kLocalServerName));
    m_localServer->listen(QLatin1String(kLocalServerName));
    connect(m_localServer, &QLocalServer::newConnection,
            this, &MainWindow::onLocalConnection);
}

void MainWindow::startHeadless(quint16 port)
{
    m_spinPort->setValue(port);

    QString err;
    if (!m_httpServer->start(port, err)) {
        appendLog(QStringLiteral("无头模式启动失败：%1").arg(err));
        return;
    }

    m_btnStartStop->setText(QStringLiteral("停止服务"));
    m_lblStatus->setText(QStringLiteral("● 运行中"));
    m_lblStatus->setObjectName("statusRunning");
    m_lblBaseUrl->setText(m_httpServer->baseUrl());
    m_spinPort->setEnabled(false);
    m_btnHeadless->setEnabled(true);
    appendLog(QStringLiteral("服务器已启动（无头模式）: %1").arg(m_httpServer->baseUrl()));
    regenerateStaticMeta();

    m_trayIcon->show();
    m_trayIcon->showMessage(QStringLiteral("升级服务器"),
                            QStringLiteral("服务器已在无头模式运行\n%1").arg(m_httpServer->baseUrl()),
                            QSystemTrayIcon::Information, 3000);
}

void MainWindow::onToggleHeadless()
{
    if (!m_httpServer->isListening()) {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("请先启动服务后再切换到无头模式。"));
        return;
    }

    hide();
    m_trayIcon->show();
    m_trayIcon->showMessage(QStringLiteral("升级服务器"),
                            QStringLiteral("已切换到无头模式运行\n双击托盘图标可恢复界面"),
                            QSystemTrayIcon::Information, 3000);
    appendLog(QStringLiteral("已切换到无头模式"));
}

void MainWindow::showFromTray()
{
    m_trayIcon->hide();
    showNormal();
    raise();
    activateWindow();
    appendLog(QStringLiteral("已从无头模式恢复界面"));
}

void MainWindow::quitFromTray()
{
    m_trayIcon->hide();
    qApp->quit();
}

void MainWindow::onTerminateProcess()
{
    if (QMessageBox::question(this, QStringLiteral("结束进程"),
                              QStringLiteral("确定要强制结束服务器进程吗？\n这将立即停止服务并释放所有文件占用。"),
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    // 停止 HTTP 服务
    if (m_httpServer) {
        m_httpServer->stop();
    }

    // 隐藏托盘图标
    if (m_trayIcon) {
        m_trayIcon->hide();
    }

    // 强制终止进程，确保释放所有文件句柄
    ::_exit(0);
}

void MainWindow::onTrayActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::DoubleClick) {
        showFromTray();
    }
}

void MainWindow::onLocalConnection()
{
    QLocalSocket *socket = m_localServer->nextPendingConnection();
    if (!socket) return;

    socket->waitForReadyRead(1000);
    const QByteArray data = socket->readAll();
    socket->close();
    socket->deleteLater();

    if (data.trimmed() == "show") {
        showFromTray();
    }
}

// ============================================================
// 历史版本管理对话框
// ============================================================

void MainWindow::onManageHistory()
{
    const int row = selectedRow();
    if (row < 0) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请先选中一个应用。"));
        return;
    }

    const QString appId = m_manager->entries().at(row).appId;

    // ---- 构建对话框 ----
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("历史版本管理 — %1").arg(appId));
    dlg.setMinimumSize(700, 420);

    auto *vlay  = new QVBoxLayout(&dlg);
    vlay->setContentsMargins(14, 14, 14, 14);
    vlay->setSpacing(10);

    // 当前最新版本提示
    const AppUpdateEntry cur = m_manager->entry(appId);
    auto *lblCur = new QLabel(
        QStringLiteral("应用：<b>%1</b>（%2）  |  当前最新版本：<b>v%3</b>  |  包文件：%4")
            .arg(cur.appName, cur.appId, cur.latestVersion, cur.packageFileName));
    lblCur->setWordWrap(true);
    vlay->addWidget(lblCur);

    // 历史版本表格
    auto *table = new QTableWidget(0, 5, &dlg);
    table->setHorizontalHeaderLabels({
        QStringLiteral("版本号"),
        QStringLiteral("历史文件名"),
        QStringLiteral("SHA256（前16位）"),
        QStringLiteral("文件状态"),
        QStringLiteral("版本说明")
    });
    table->horizontalHeader()->setStretchLastSection(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(true);
    table->verticalHeader()->setDefaultSectionSize(28);
    vlay->addWidget(table);

    // 刷新表格内容的 lambda
    auto refreshHistTable = [&]() {
        const QVector<HistoryVersion> hist = m_manager->historyVersions(appId);
        table->setRowCount(hist.size());
        const QString histDir = m_manager->historyDirForApp(appId);
        for (int i = 0; i < hist.size(); ++i) {
            const HistoryVersion &hv = hist.at(i);
            const bool fileExists = QFileInfo::exists(
                QDir(histDir).absoluteFilePath(hv.fileName));
            table->setItem(i, 0, new QTableWidgetItem(QStringLiteral("v") + hv.version));
            table->setItem(i, 1, new QTableWidgetItem(hv.fileName));
            const QString sha16 = hv.sha256.left(16) + (hv.sha256.size() > 16 ? QStringLiteral("...") : QString());
            table->setItem(i, 2, new QTableWidgetItem(sha16));
            auto *statusItem = new QTableWidgetItem(
                fileExists ? QStringLiteral("[OK] 文件存在") : QStringLiteral("[!] 文件缺失"));
            statusItem->setForeground(fileExists ? QColor(22, 163, 74) : QColor(220, 38, 38));
            table->setItem(i, 3, statusItem);
            // 版本说明（截断显示）
            const QString logPreview = hv.changeLog.length() > 40
                ? hv.changeLog.left(40) + QStringLiteral("...")
                : hv.changeLog;
            auto *logItem = new QTableWidgetItem(logPreview);
            logItem->setToolTip(hv.changeLog);
            table->setItem(i, 4, logItem);
        }
        table->resizeColumnsToContents();
    };
    refreshHistTable();

    // 按钮栏
    auto *hlay = new QHBoxLayout;

    // 归档当前版本按钮
    auto *btnArchive = new QPushButton(QStringLiteral("归档当前版本"));
    btnArchive->setToolTip(
        QStringLiteral("将当前最新版本（%1）立即归档为历史版本，\n"
                       "编辑应用更改版本号时也会自动归档。").arg(cur.latestVersion));
    hlay->addWidget(btnArchive);

    // 手动添加按钮
    auto *btnAdd = new QPushButton(QStringLiteral("手动添加"));
    btnAdd->setToolTip(QStringLiteral("选择历史包文件并填写版本号，手动添加一条历史记录。"));
    hlay->addWidget(btnAdd);

    // 删除按钮
    auto *btnDel = new QPushButton(QStringLiteral("删除选中"));
    btnDel->setToolTip(QStringLiteral("删除选中的历史版本记录，可选是否同时删除磁盘文件。"));
    hlay->addWidget(btnDel);

    // 编辑版本说明
    auto *btnEditLog = new QPushButton(QStringLiteral("编辑说明"));
    btnEditLog->setToolTip(QStringLiteral("编辑选中历史版本的更新说明。"));
    hlay->addWidget(btnEditLog);

    hlay->addStretch();
    auto *btnClose = new QPushButton(QStringLiteral("关闭"));
    hlay->addWidget(btnClose);
    vlay->addLayout(hlay);

    // ---- 归档当前版本 ----
    connect(btnArchive, &QPushButton::clicked, [&]() {
        QString archiveErr;
        if (m_manager->archiveCurrentVersion(appId, archiveErr)) {
            QString saveErr;
            m_manager->save(saveErr);
            appendLog(QStringLiteral("手动归档历史版本: %1 v%2").arg(appId, cur.latestVersion));
            refreshHistTable();
            lblCur->setText(QStringLiteral("应用：<b>%1</b>（%2）  |  当前最新版本：<b>v%3</b>  |  包文件：%4")
                .arg(cur.appName, cur.appId, cur.latestVersion, cur.packageFileName));
            QMessageBox::information(&dlg, QStringLiteral("归档成功"),
                QStringLiteral("v%1 已归档为历史版本。").arg(cur.latestVersion));
        } else {
            QMessageBox::warning(&dlg, QStringLiteral("归档失败"), archiveErr);
        }
    });

    // ---- 手动添加历史版本 ----
    connect(btnAdd, &QPushButton::clicked, [&]() {
        // 1. 选择文件
        const QString filePath = QFileDialog::getOpenFileName(
            &dlg,
            QStringLiteral("选择历史版本包文件"),
            m_manager->packagesDir(),
            QStringLiteral("升级包 (*.exe *.zip);;所有文件 (*)"));
        if (filePath.isEmpty()) return;

        // 2. 输入版本号
        bool ok = false;
        const QString version = QInputDialog::getText(
            &dlg,
            QStringLiteral("输入版本号"),
            QStringLiteral("请输入该历史版本的版本号（如 1.2.3）："),
            QLineEdit::Normal, QString(), &ok).trimmed();
        if (!ok || version.isEmpty()) return;

        // 2.5 输入版本说明（可选）
        bool okLog = false;
        const QString changeLog = QInputDialog::getMultiLineText(
            &dlg,
            QStringLiteral("输入版本说明"),
            QStringLiteral("请输入该版本的更新说明（可留空）："),
            QString(), &okLog).trimmed();
        if (!okLog) return;

        // 3. 检查版本号是否已存在
        const QVector<HistoryVersion> hist = m_manager->historyVersions(appId);
        for (const HistoryVersion &hv : hist) {
            if (hv.version == version) {
                QMessageBox::warning(&dlg, QStringLiteral("版本已存在"),
                    QStringLiteral("历史记录中已存在版本 v%1，请使用其他版本号。").arg(version));
                return;
            }
        }

        // 4. 目标路径：packages/history/{appId}/{originalFileName}
        const QString histDir = m_manager->historyDirForApp(appId);
        QDir().mkpath(histDir);
        const QString srcName  = QFileInfo(filePath).fileName();
        const QString destPath = QDir(histDir).absoluteFilePath(srcName);

        // 若目标位置已有同名文件，询问是否覆盖
        if (QFileInfo::exists(destPath)) {
            int ans = QMessageBox::question(&dlg, QStringLiteral("文件已存在"),
                QStringLiteral("历史目录已存在文件 %1，是否覆盖？").arg(srcName));
            if (ans != QMessageBox::Yes) return;
            QFile::remove(destPath);
        }

        if (filePath != destPath) {
            if (!QFile::copy(filePath, destPath)) {
                QMessageBox::warning(&dlg, QStringLiteral("复制失败"),
                    QStringLiteral("无法将文件复制到历史目录：\n%1")
                        .arg(QDir::toNativeSeparators(destPath)));
                return;
            }
        }

        // 5. 计算 SHA256
        const QString sha256 = AppUpdateManager::computeFileSha256(destPath);

        // 6. 在内存中插入记录（直接修改 entry）
        AppUpdateEntry e = m_manager->entry(appId);
        HistoryVersion hv;
        hv.version  = version;
        hv.fileName = srcName;
        hv.sha256   = sha256;
        hv.changeLog = changeLog;
        e.history.append(hv);

        QString upsertErr;
        // upsertEntry 会重新计算当前包的 sha256，我们只需要它保存 history 字段
        // 直接调用 save()/load() 不合适，改用内部接口：先 upsertEntry 把完整 entry 写入
        if (!m_manager->upsertEntry(e, upsertErr)) {
            // upsertEntry 可能因当前包文件检查失败，只需对 history 部分持久化
            // 改为手工追加 + save
            // 这里用 removeEntry + re-insert 规避 sha256 检查
            QMessageBox::warning(&dlg, QStringLiteral("保存失败"),
                QStringLiteral("历史记录保存失败：%1").arg(upsertErr));
            QFile::remove(destPath);
            return;
        }

        QString saveErr;
        m_manager->save(saveErr);
        appendLog(QStringLiteral("手动添加历史版本: %1 v%2 (%3)").arg(appId, version, srcName));
        refreshHistTable();
        QMessageBox::information(&dlg, QStringLiteral("添加成功"),
            QStringLiteral("v%1 已添加到历史版本列表。").arg(version));
    });

    // ---- 删除选中 ----
    connect(btnDel, &QPushButton::clicked, [&]() {
        const int selRow = table->currentRow();
        if (selRow < 0) {
            QMessageBox::information(&dlg, QStringLiteral("提示"), QStringLiteral("请先选中一行。"));
            return;
        }

        const QVector<HistoryVersion> hist = m_manager->historyVersions(appId);
        if (selRow >= hist.size()) return;
        const HistoryVersion &hv = hist.at(selRow);

        const int ans = QMessageBox::question(
            &dlg,
            QStringLiteral("确认删除"),
            QStringLiteral("确定要删除历史版本 v%1（%2）的记录吗？\n\n"
                           "点击 [Yes] 只删除记录，点击 [No] 同时删除磁盘文件。").arg(hv.version, hv.fileName),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
            QMessageBox::Cancel);
        if (ans == QMessageBox::Cancel) return;

        // 删除磁盘文件（选 No）
        if (ans == QMessageBox::No) {
            const QString histDir = m_manager->historyDirForApp(appId);
            const QString filePath = QDir(histDir).absoluteFilePath(hv.fileName);
            if (QFileInfo::exists(filePath) && !QFile::remove(filePath)) {
                QMessageBox::warning(&dlg, QStringLiteral("删除文件失败"),
                    QStringLiteral("无法删除文件：\n%1").arg(QDir::toNativeSeparators(filePath)));
                return;
            }
        }

        // 从内存记录中移除（直接操作，不经过 upsertEntry 避免空历史被恢复）
        m_manager->removeHistoryVersion(appId, selRow);

        QString saveErr;
        m_manager->save(saveErr);
        appendLog(QStringLiteral("已删除历史版本: %1 v%2").arg(appId, hv.version));
        refreshHistTable();
    });

    // ---- 编辑版本说明 ----
    connect(btnEditLog, &QPushButton::clicked, [&]() {
        const int selRow = table->currentRow();
        if (selRow < 0) {
            QMessageBox::information(&dlg, QStringLiteral("提示"), QStringLiteral("请先选中一行。"));
            return;
        }
        const QVector<HistoryVersion> hist = m_manager->historyVersions(appId);
        if (selRow >= hist.size()) return;
        const HistoryVersion &hv = hist.at(selRow);

        bool ok = false;
        const QString newLog = QInputDialog::getMultiLineText(
            &dlg,
            QStringLiteral("编辑版本说明 — v%1").arg(hv.version),
            QStringLiteral("更新说明："),
            hv.changeLog, &ok).trimmed();
        if (!ok) return;

        m_manager->updateHistoryChangeLog(appId, selRow, newLog);
        QString saveErr;
        m_manager->save(saveErr);
        appendLog(QStringLiteral("已编辑历史版本说明: %1 v%2").arg(appId, hv.version));
        refreshHistTable();
    });

    connect(btnClose, &QPushButton::clicked, &dlg, &QDialog::accept);

    dlg.exec();
}

// ============================================================
// 用户管理
// ============================================================

void MainWindow::refreshUserTable()
{
    const QList<User> users = m_userManager->users();
    m_userTable->setRowCount(users.size());
    for (int i = 0; i < users.size(); ++i) {
        const User &u = users[i];
        m_userTable->setItem(i, 0, new QTableWidgetItem(u.username));
        m_userTable->setItem(i, 1, new QTableWidgetItem(
            u.role == QStringLiteral("admin") ? QStringLiteral("管理员") : QStringLiteral("普通用户")));
        m_userTable->setItem(i, 2, new QTableWidgetItem(
            u.allowedApps.isEmpty() ? QStringLiteral("全部") : u.allowedApps.join(QStringLiteral(", "))));
        // 在 item 数据中保存 user id 用于后续操作
        m_userTable->item(i, 0)->setData(Qt::UserRole, u.id);
    }
    m_userTable->resizeColumnsToContents();
}

int MainWindow::selectedUserRow() const
{
    const QList<QTableWidgetSelectionRange> sel = m_userTable->selectedRanges();
    return sel.isEmpty() ? -1 : sel.first().topRow();
}

void MainWindow::onRefreshUserTable()
{
    refreshUserTable();
}

void MainWindow::onAddUser()
{
    User empty;
    UserEditDialog dlg(QStringLiteral("新增用户"), empty, this);
    if (dlg.exec() != QDialog::Accepted) return;

    User u = dlg.result();
    QString err;
    if (!m_userManager->addUser(u, err)) {
        QMessageBox::warning(this, QStringLiteral("新增失败"), err);
        return;
    }
    if (!m_userManager->save(err)) {
        QMessageBox::warning(this, QStringLiteral("保存失败"), err);
    }
    refreshUserTable();
}

void MainWindow::onEditUser()
{
    const int row = selectedUserRow();
    if (row < 0) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请先选择一个用户。"));
        return;
    }
    const QString userId = m_userTable->item(row, 0)->data(Qt::UserRole).toString();
    User u = m_userManager->user(userId);
    if (u.id.isEmpty()) return;

    UserEditDialog dlg(QStringLiteral("编辑用户"), u, this);
    if (dlg.exec() != QDialog::Accepted) return;

    User updated = dlg.result();
    updated.id = userId;
    QString err;
    if (!m_userManager->updateUser(updated, err)) {
        QMessageBox::warning(this, QStringLiteral("更新失败"), err);
        return;
    }
    if (!m_userManager->save(err)) {
        QMessageBox::warning(this, QStringLiteral("保存失败"), err);
    }
    refreshUserTable();
}

void MainWindow::onRemoveUser()
{
    const int row = selectedUserRow();
    if (row < 0) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请先选择一个用户。"));
        return;
    }
    const QString userId   = m_userTable->item(row, 0)->data(Qt::UserRole).toString();
    const QString username = m_userTable->item(row, 0)->text();

    const int ans = QMessageBox::question(this, QStringLiteral("确认删除"),
        QStringLiteral("确定删除用户「%1」？").arg(username),
        QMessageBox::Yes | QMessageBox::No);
    if (ans != QMessageBox::Yes) return;

    m_userManager->removeUser(userId);
    QString err;
    if (!m_userManager->save(err)) {
        QMessageBox::warning(this, QStringLiteral("保存失败"), err);
    }
    refreshUserTable();
}
