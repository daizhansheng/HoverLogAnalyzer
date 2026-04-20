// PressAnalyzerDbViewer.cpp - Database viewer functions
#include "PressAnalyzer.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QSqlQuery>
#include <QSqlError>
#include <QStyledItemDelegate>
#include <QMessageBox>
#include <QPushButton>
#include <QLabel>
#include <QDebug>
#include <QApplication>
#include <QClipboard>

class BlobDecimalDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QString displayText(const QVariant &value, const QLocale &) const override {
        if (value.type() == QVariant::ByteArray) {
            const QByteArray ba = value.toByteArray();
            if (ba.isEmpty()) return QString();
            QStringList parts;
            parts.reserve(ba.size());
            for (unsigned char byte : ba) {
                parts.append(QString::number(static_cast<int>(byte)));
            }
            return parts.join(',');
        }
        return value.toString();
    }
};

void PressAnalyzer::setupDbViewerDock()
{
    // dbViewerWidget 已在 setupCentralWidget() 中创建并加入 centralStack (index 1)
    QVBoxLayout *mainLayout = new QVBoxLayout(dbViewerWidget);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    // 顶部一行：[文件名] [表标签滚动区(中间)] [关闭按钮]
    QHBoxLayout *topLayout = new QHBoxLayout();
    topLayout->setSpacing(6);

    QLabel *dbPathLabel = new QLabel("未打开数据库");
    dbPathLabel->setObjectName("dbPathLabel");
    dbPathLabel->setStyleSheet("color: #444; font-size: 12px; font-weight: bold;");
    dbPathLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    dbPathLabel->setWordWrap(false);
    dbPathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    // 表名标签区：QScrollArea 内放 QHBoxLayout + QPushButton，窗口缩小时横向滚动
    QScrollArea *tableScrollArea = new QScrollArea();
    tableScrollArea->setObjectName("dbTableScrollArea");
    tableScrollArea->setFrameShape(QFrame::NoFrame);
    tableScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    tableScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tableScrollArea->setFixedHeight(28);
    tableScrollArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    tableScrollArea->setStyleSheet(
        "QScrollArea { background: transparent; border: none; }"
        "QScrollBar:horizontal { height: 6px; }"
    );

    QWidget *tableButtonBar = new QWidget();
    tableButtonBar->setObjectName("dbTableButtonBar");
    tableButtonBar->setStyleSheet("background: transparent;");
    QHBoxLayout *tableBarLayout = new QHBoxLayout(tableButtonBar);
    tableBarLayout->setContentsMargins(0, 0, 0, 0);
    tableBarLayout->setSpacing(4);
    tableBarLayout->addStretch();   // 初始占位，有表时清掉重填
    tableScrollArea->setWidget(tableButtonBar);
    tableScrollArea->setWidgetResizable(true);

    // dbTableList 保留为空 QListWidget（供 loadDbTable 兼容），但不显示
    dbTableList = new QListWidget();
    dbTableList->hide();

    QPushButton *closeDbBtn = new QPushButton("关闭数据库");
    closeDbBtn->setFixedWidth(90);
    closeDbBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    closeDbBtn->setStyleSheet(
        "QPushButton { background:#888; color:white; border:none; border-radius:3px; padding:3px 8px; font-size:12px; }"
        "QPushButton:hover { background:#999; }"
        "QPushButton:pressed { background:#777; }"
    );

    topLayout->addWidget(dbPathLabel);
    topLayout->addWidget(tableScrollArea, 1);
    topLayout->addWidget(closeDbBtn);
    mainLayout->addLayout(topLayout);

    // 表格
    dbTableView = new QTableView();
    dbTableView->setAlternatingRowColors(true);
    dbTableView->setSelectionBehavior(QAbstractItemView::SelectItems);
    dbTableView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    dbTableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    dbTableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    dbTableView->horizontalHeader()->setStretchLastSection(true);
    dbTableView->setStyleSheet(
        "QTableView { border: 1px solid #ddd; font-size: 12px; }"
        "QHeaderView::section { background: #f0f0f0; border: 1px solid #ccc; padding: 3px; font-weight: bold; }"
    );
    dbTableView->setSortingEnabled(true);

    // 复制逻辑
    auto doCopy = [this](){
        QItemSelectionModel *sel = dbTableView->selectionModel();
        if (!sel || !dbTableModel) return;
        QModelIndexList indexes = sel->selectedIndexes();
        if (indexes.isEmpty()) return;

        std::sort(indexes.begin(), indexes.end(), [](const QModelIndex &a, const QModelIndex &b){
            return a.row() != b.row() ? a.row() < b.row() : a.column() < b.column();
        });

        QString result;
        int prevRow = indexes.first().row();
        for (const QModelIndex &idx : indexes) {
            if (idx.row() != prevRow) {
                result += '\n';
                prevRow = idx.row();
            } else if (!result.isEmpty()) {
                result += '\t';
            }
            QVariant v = dbTableModel->data(idx, Qt::DisplayRole);
            if (v.type() == QVariant::ByteArray) {
                const QByteArray ba = v.toByteArray();
                QStringList parts;
                for (unsigned char byte : ba)
                    parts.append(QString::number(static_cast<int>(byte)));
                result += parts.join(',');
            } else {
                result += v.toString();
            }
        }
        QApplication::clipboard()->setText(result);
    };

    // 右键菜单
    dbTableView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(dbTableView, &QTableView::customContextMenuRequested, this, [this, doCopy](const QPoint &pos){
        QItemSelectionModel *sel = dbTableView->selectionModel();
        bool hasSelection = sel && !sel->selectedIndexes().isEmpty();
        QMenu menu(dbTableView);
        QAction *actCopy = menu.addAction("Copy");
        actCopy->setShortcut(QKeySequence::Copy);
        actCopy->setEnabled(hasSelection);
        connect(actCopy, &QAction::triggered, this, doCopy);
        menu.exec(dbTableView->viewport()->mapToGlobal(pos));
    });

    // Cmd+C / Ctrl+C：在 dbTableView 和其 viewport 双层都装 filter
    // 用局部 struct，parent 设为 dbTableView 自动管理生命周期
    struct CopyFilter : public QObject {
        std::function<void()> fn;
        CopyFilter(QObject *parent, std::function<void()> f) : QObject(parent), fn(std::move(f)) {}
        bool eventFilter(QObject *, QEvent *e) override {
            if (e->type() == QEvent::KeyPress) {
                QKeyEvent *ke = static_cast<QKeyEvent*>(e);
                if (ke->matches(QKeySequence::Copy)) {
                    fn();
                    return true;
                }
            }
            return false;
        }
    };
    auto *cf = new CopyFilter(dbTableView, doCopy);
    dbTableView->installEventFilter(cf);
    dbTableView->viewport()->installEventFilter(cf);

    QLabel *rowCountLabel = new QLabel("行数: 0");
    rowCountLabel->setObjectName("dbRowCountLabel");
    rowCountLabel->setStyleSheet("color:#666; font-size:11px; padding:2px;");

    mainLayout->addWidget(dbTableView, 1);
    mainLayout->addWidget(rowCountLabel);

    // 关闭按钮
    connect(closeDbBtn, &QPushButton::clicked, this, [this](){
        centralStack->setCurrentIndex(0);
    });
}

void PressAnalyzer::openDatabaseFile(const QString &filePath)
{
    if (filePath.isEmpty()) return;

    // 关闭旧连接
    if (currentDb.isOpen()) {
        currentDb.close();
    }
    QString connName = "dbviewer_conn";
    if (QSqlDatabase::contains(connName)) {
        QSqlDatabase::removeDatabase(connName);
    }

    currentDb = QSqlDatabase::addDatabase("QSQLITE", connName);
    currentDb.setDatabaseName(filePath);

    if (!currentDb.open()) {
        QMessageBox::warning(this, "打开失败",
            QString("无法打开数据库文件：\n%1\n\n错误：%2")
                .arg(filePath)
                .arg(currentDb.lastError().text()));
        return;
    }

    currentDbPath = filePath;

    // 更新路径标签
    QLabel *pathLabel = dbViewerWidget->findChild<QLabel*>("dbPathLabel");
    if (pathLabel) {
        QFileInfo fi(filePath);
        pathLabel->setText(fi.fileName());
        pathLabel->setToolTip(filePath);
    }

    // 列举所有表，填充顶部标签按钮
    QStringList tables = currentDb.tables();
    if (tables.isEmpty()) {
        QMessageBox::information(this, "提示", "该数据库中没有找到任何表。");
    }

    // 清空旧按钮
    QWidget *btnBar = dbViewerWidget->findChild<QWidget*>("dbTableButtonBar");
    if (btnBar) {
        QLayout *oldLayout = btnBar->layout();
        QLayoutItem *item;
        while (oldLayout && (item = oldLayout->takeAt(0))) {
            delete item->widget();
            delete item;
        }
        // 重新填充
        QHBoxLayout *barLayout = qobject_cast<QHBoxLayout*>(oldLayout);
        if (!barLayout) {
            barLayout = new QHBoxLayout(btnBar);
            barLayout->setContentsMargins(0, 0, 0, 0);
            barLayout->setSpacing(4);
        }
        for (const QString &tbl : tables) {
            QPushButton *btn = new QPushButton(tbl, btnBar);
            btn->setMinimumWidth(btn->fontMetrics().horizontalAdvance(tbl) + 24);
            btn->setFixedHeight(22);
            btn->setCheckable(true);
            btn->setStyleSheet(
                "QPushButton { padding: 2px 10px; border: 1px solid #ccc; border-radius: 3px;"
                "  background: #fff; font-size: 12px; }"
                "QPushButton:hover { background: #e8f0fe; }"
                "QPushButton:checked { background: #4A90D9; color: white; border-color: #4A90D9; }"
            );
            connect(btn, &QPushButton::clicked, this, [this, tbl, btnBar](bool){
                // 取消其他按钮选中
                for (QPushButton *b : btnBar->findChildren<QPushButton*>())
                    b->setChecked(false);
                QPushButton *self = qobject_cast<QPushButton*>(sender());
                if (self) self->setChecked(true);
                loadDbTable(tbl);
            });
            barLayout->addWidget(btn);
        }
        barLayout->addStretch();
    }

    // 清空旧表格
    if (dbTableModel) {
        dbTableView->setModel(nullptr);
        delete dbTableModel;
        dbTableModel = nullptr;
    }
    QLabel *rowLbl = dbViewerWidget->findChild<QLabel*>("dbRowCountLabel");
    if (rowLbl) rowLbl->setText("行数: 0");

    // 自动加载第一张表，并选中第一个按钮
    if (!tables.isEmpty()) {
        if (btnBar) {
            QPushButton *first = btnBar->findChild<QPushButton*>();
            if (first) first->setChecked(true);
        }
        loadDbTable(tables.first());
    }

    // 切换到 DB 查看器页面
    centralStack->setCurrentIndex(1);
    if (statusPathLabel) statusPathLabel->setText(filePath);
}

void PressAnalyzer::loadDbTable(const QString &tableName)
{
    if (!currentDb.isOpen()) return;

    // 清理旧模型
    if (dbTableModel) {
        dbTableView->setModel(nullptr);
        delete dbTableModel;
        dbTableModel = nullptr;
    }

    dbTableModel = new QSqlTableModel(this, currentDb);
    dbTableModel->setTable(tableName);
    dbTableModel->setEditStrategy(QSqlTableModel::OnManualSubmit);
    dbTableModel->select();

    // 若数据超过 10000 行，分批加载全部行
    while (dbTableModel->canFetchMore()) {
        dbTableModel->fetchMore();
    }

    dbTableView->setModel(dbTableModel);
    // 应用 BLOB 十进制委托，将二进制列显示为逗号分隔的十进制字节
    dbTableView->setItemDelegate(new BlobDecimalDelegate(dbTableView));
    dbTableView->resizeColumnsToContents();

    // 更新行数标签
    QLabel *rowLbl = dbViewerWidget->findChild<QLabel*>("dbRowCountLabel");
    if (rowLbl) {
        // 从数据库直接查询精确行数
        QSqlQuery q(currentDb);
        q.exec(QString("SELECT COUNT(*) FROM \"%1\"").arg(tableName));
        int rowCount = 0;
        if (q.next()) rowCount = q.value(0).toInt();
        rowLbl->setText(QString("表: %1   行数: %2   列数: %3")
                            .arg(tableName)
                            .arg(rowCount)
                            .arg(dbTableModel->columnCount()));
    }
}
