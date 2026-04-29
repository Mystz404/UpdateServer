#include "docwindow.h"
#include "doceditdialog.h"
#include "docmanager.h"

#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QVBoxLayout>

DocWindow::DocWindow(DocManager *manager, QWidget *parent)
    : QWidget(parent)
    , m_manager(manager)
{
    buildUi();
    refreshTable();
}

void DocWindow::buildUi()
{
    auto *vlay = new QVBoxLayout(this);
    vlay->setContentsMargins(0, 0, 0, 0);
    vlay->setSpacing(8);

    // 按钮栏
    auto *btnBar = new QHBoxLayout;
    m_btnAdd     = new QPushButton(QStringLiteral("新增文档"), this);
    m_btnEdit    = new QPushButton(QStringLiteral("编辑"),     this);
    m_btnRemove  = new QPushButton(QStringLiteral("删除"),     this);
    m_btnRefresh = new QPushButton(QStringLiteral("刷新"),     this);

    btnBar->addWidget(m_btnAdd);
    btnBar->addWidget(m_btnEdit);
    btnBar->addWidget(m_btnRemove);
    btnBar->addWidget(m_btnRefresh);
    btnBar->addStretch();
    vlay->addLayout(btnBar);

    // 表格
    m_table = new QTableWidget(0, 7, this);
    m_table->setHorizontalHeaderLabels({
        QStringLiteral("文档ID"),
        QStringLiteral("标题"),
        QStringLiteral("版本"),
        QStringLiteral("分类"),
        QStringLiteral("文件名"),
        QStringLiteral("排序权重"),
        QStringLiteral("SHA256（前16位）")
    });
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setDefaultSectionSize(28);
    vlay->addWidget(m_table, 1);

    connect(m_btnAdd,    &QPushButton::clicked, this, &DocWindow::onAddDoc);
    connect(m_btnEdit,   &QPushButton::clicked, this, &DocWindow::onEditDoc);
    connect(m_btnRemove, &QPushButton::clicked, this, &DocWindow::onRemoveDoc);
    connect(m_btnRefresh,&QPushButton::clicked, this, &DocWindow::onRefresh);
}

void DocWindow::refreshTable()
{
    const QList<DocEntry> entries = m_manager->entries();
    m_table->setRowCount(entries.size());

    for (int i = 0; i < entries.size(); ++i) {
        const DocEntry &e = entries.at(i);
        m_table->setItem(i, 0, new QTableWidgetItem(e.docId));
        m_table->setItem(i, 1, new QTableWidgetItem(e.title));
        m_table->setItem(i, 2, new QTableWidgetItem(e.version));
        m_table->setItem(i, 3, new QTableWidgetItem(e.categories.join(QStringLiteral(", "))));
        m_table->setItem(i, 4, new QTableWidgetItem(e.fileName));
        m_table->setItem(i, 5, new QTableWidgetItem(QString::number(e.sortOrder)));

        const QString sha16 = e.sha256.left(16) + (e.sha256.length() > 16 ? QStringLiteral("…") : QString());
        m_table->setItem(i, 6, new QTableWidgetItem(sha16));
    }
    m_table->resizeColumnsToContents();
}

int DocWindow::selectedRow() const
{
    const auto sel = m_table->selectionModel()->selectedRows();
    return sel.isEmpty() ? -1 : sel.first().row();
}

// ============================================================
// 新增文档
// ============================================================
void DocWindow::onAddDoc()
{
    DocEntry entry;
    DocEditDialog dlg(QStringLiteral("新增文档"), entry, m_manager->docsDir(), this);
    if (dlg.exec() != QDialog::Accepted) return;

    entry = dlg.result();
    QString err;
    if (!m_manager->upsertEntry(entry, err)) {
        QMessageBox::warning(this, QStringLiteral("新增失败"), err);
        return;
    }
    m_manager->save(err);
    refreshTable();
}

// ============================================================
// 编辑文档
// ============================================================
void DocWindow::onEditDoc()
{
    const int row = selectedRow();
    if (row < 0) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请先选中一行。"));
        return;
    }

    DocEntry entry = m_manager->entries().at(row);
    DocEditDialog dlg(QStringLiteral("编辑文档"), entry, m_manager->docsDir(), this);
    if (dlg.exec() != QDialog::Accepted) return;

    entry = dlg.result();
    QString err;
    if (!m_manager->upsertEntry(entry, err)) {
        QMessageBox::warning(this, QStringLiteral("编辑失败"), err);
        return;
    }
    m_manager->save(err);
    refreshTable();
}

// ============================================================
// 删除文档
// ============================================================
void DocWindow::onRemoveDoc()
{
    const int row = selectedRow();
    if (row < 0) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请先选中一行。"));
        return;
    }

    const QString docId = m_manager->entries().at(row).docId;
    const int ret = QMessageBox::question(this,
        QStringLiteral("确认删除"),
        QStringLiteral("确定要删除文档「%1」吗？\n\n注意：此操作只删除记录，不会删除磁盘上的 PDF 文件。")
            .arg(docId));
    if (ret != QMessageBox::Yes) return;

    m_manager->removeEntry(docId);
    QString err;
    m_manager->save(err);
    refreshTable();
}

// ============================================================
// 刷新
// ============================================================
void DocWindow::onRefresh()
{
    refreshTable();
}
