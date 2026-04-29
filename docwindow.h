#ifndef DOCWINDOW_H
#define DOCWINDOW_H

#include "docentry.h"

#include <QTableWidget>
#include <QPushButton>
#include <QWidget>

class DocManager;

/**
 * @brief 文档管理面板。
 *
 * 嵌入到 MainWindow，展示文档列表并提供新增/编辑/删除操作。
 */
class DocWindow : public QWidget
{
    Q_OBJECT

public:
    explicit DocWindow(DocManager *manager, QWidget *parent = nullptr);

private slots:
    void onAddDoc();
    void onEditDoc();
    void onRemoveDoc();
    void onRefresh();

private:
    void buildUi();
    void refreshTable();
    int selectedRow() const;

    DocManager   *m_manager  = nullptr;

    QTableWidget *m_table    = nullptr;
    QPushButton  *m_btnAdd   = nullptr;
    QPushButton  *m_btnEdit  = nullptr;
    QPushButton  *m_btnRemove= nullptr;
    QPushButton  *m_btnRefresh = nullptr;
};

#endif // DOCWINDOW_H
