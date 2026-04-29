#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSystemTrayIcon>
#include <QTableWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QLabel>
#include <QPlainTextEdit>

class AppUpdateManager;
class DocManager;
class DocWindow;
class HttpServer;
class QLocalServer;
class UserManager;

/**
 * @brief 升级服务器管理界面。
 *
 * 功能：
 * - 展示所有已注册应用及其升级包信息（表格）；
 * - 新增 / 编辑 / 删除应用条目；
 * - 选择升级包文件（文件对话框）；
 * - 启动 / 停止 HTTP 服务；
 * - 实时日志输出。
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    /// 以无头模式启动：自动开始监听并隐藏窗口
    void startHeadless(quint16 port);

private slots:
    void onStartStopServer();
    void onAddApp();
    void onEditApp();
    void onRemoveApp();
    void onRefreshTable();
    void onManageHistory();
    void onRequestReceived(const QString &method, const QString &path, int statusCode);

    // 用户管理
    void onAddUser();
    void onEditUser();
    void onRemoveUser();
    void onRefreshUserTable();

    // 无头模式
    void onToggleHeadless();
    void showFromTray();
    void quitFromTray();

    // 结束进程
    void onTerminateProcess();
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
    void onLocalConnection();

private:
    void setupUi();
    void setupTrayIcon();
    void setupLocalServer();
    void applyStyleSheet();
    void refreshTable();
    void refreshUserTable();
    void appendLog(const QString &text);
    int selectedRow() const;
    int selectedUserRow() const;
    void regenerateStaticMeta();

    // 核心组件
    AppUpdateManager *m_manager     = nullptr;
    DocManager       *m_docManager  = nullptr;
    HttpServer       *m_httpServer  = nullptr;
    UserManager      *m_userManager = nullptr;

    // 应用管理控件
    QTableWidget *m_table = nullptr;
    DocWindow    *m_docWindow = nullptr;
    QPushButton *m_btnStartStop = nullptr;
    QPushButton *m_btnDocs = nullptr;
    QPushButton *m_btnAdd = nullptr;
    QPushButton *m_btnEdit = nullptr;
    QPushButton *m_btnRemove = nullptr;
    QPushButton *m_btnHistory = nullptr;
    QPushButton *m_btnRefresh = nullptr;
    QSpinBox *m_spinPort = nullptr;
    QLabel *m_lblStatus = nullptr;
    QLabel *m_lblBaseUrl = nullptr;
    QPlainTextEdit *m_logView = nullptr;

    // 用户管理控件
    QTableWidget *m_userTable   = nullptr;
    QPushButton  *m_btnUserAdd  = nullptr;
    QPushButton  *m_btnUserEdit = nullptr;
    QPushButton  *m_btnUserDel  = nullptr;

    // 无头模式
    QPushButton     *m_btnHeadless  = nullptr;
    QSystemTrayIcon *m_trayIcon     = nullptr;
    QLocalServer    *m_localServer  = nullptr;
};

#endif // MAINWINDOW_H
