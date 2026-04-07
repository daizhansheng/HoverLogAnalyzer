#ifndef DYNAMICCHARTMANAGER_H
#define DYNAMICCHARTMANAGER_H

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QScrollArea>
#include <QLineEdit>
#include <QPushButton>
#include <QListWidget>
#include <QLabel>
#include <QFrame>
#include <QStringList>
#include <QVector>
#include <QDateTime>
#include <QMap>
#include <QSet>
#include <QRegularExpression>
#include <algorithm>
#include "DynamicChartWidget.h"

// ============================================================
// ChartCard
//
// 单张图表卡片：包含标题栏（标题文字 + 关闭按钮）和 DynamicChartWidget。
// ============================================================
class ChartCard : public QFrame {
    Q_OBJECT
public:
    explicit ChartCard(const QString &title, QWidget *parent = nullptr)
        : QFrame(parent)
    {
        setFrameShape(QFrame::StyledPanel);
        setFrameShadow(QFrame::Raised);
        setStyleSheet(
            "ChartCard {"
            "  background:#ffffff;"
            "  border:1px solid #d0d0d0;"
            "  border-radius:6px;"
            "}"
        );

        auto *vl = new QVBoxLayout(this);
        vl->setContentsMargins(0, 0, 0, 0);
        vl->setSpacing(0);

        // ---- 标题栏 ----
        auto *titleBar = new QWidget(this);
        titleBar->setFixedHeight(28);
        titleBar->setStyleSheet(
            "background:#EDF2FF;"
            "border-bottom:1px solid #d0d0d0;"
            "border-top-left-radius:6px;"
            "border-top-right-radius:6px;"
        );
        auto *titleLayout = new QHBoxLayout(titleBar);
        titleLayout->setContentsMargins(8, 0, 4, 0);
        titleLayout->setSpacing(4);

        m_titleLabel = new QLabel(title, titleBar);
        m_titleLabel->setStyleSheet("font-size:12px; font-weight:bold; color:#1F2D3D; background:transparent;");
        titleLayout->addWidget(m_titleLabel, 1);

        auto *closeBtn = new QPushButton("✕", titleBar);
        closeBtn->setFixedSize(20, 20);
        closeBtn->setStyleSheet(
            "QPushButton { background:transparent; border:none; color:#666; font-size:11px; }"
            "QPushButton:hover { background:#fce; color:#c00; border-radius:3px; }"
        );
        connect(closeBtn, &QPushButton::clicked, this, [this](){ emit closeRequested(); });
        titleLayout->addWidget(closeBtn);

        vl->addWidget(titleBar);

        // ---- 图表 ----
        m_chart = new DynamicChartWidget(this);
        vl->addWidget(m_chart, 1);
    }

    DynamicChartWidget *chart() { return m_chart; }
    void setTitle(const QString &t) { m_titleLabel->setText(t); }

signals:
    void closeRequested();

private:
    QLabel             *m_titleLabel = nullptr;
    DynamicChartWidget *m_chart      = nullptr;
};

// ============================================================
// DynamicChartManager
//
// 单一管理窗口。点击 chartSearchButton 时若已存在则 raise()，
// 否则创建。
//
// 顶部：搜索栏 + 字段选择列表 + 「添加到图表」按钮
// 下方：可滚动网格区域，动态布局：
//   - 1张：居中占满
//   - 2张：左右各半
//   - 3张+：每行 2 列，自动换行
// ============================================================
class DynamicChartManager : public QWidget {
    Q_OBJECT
public:
    explicit DynamicChartManager(const QStringList &logLines,
                                 QWidget            *parent = nullptr)
        : QWidget(parent, Qt::Window)
        , m_logLines(logLines)
    {
        setWindowTitle("动态图表");
        setMinimumSize(800, 600);
        resize(1000, 700);
        setAttribute(Qt::WA_DeleteOnClose, false);

        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(8, 8, 8, 8);
        root->setSpacing(6);

        // ---- 搜索行 ----
        auto *searchRow = new QHBoxLayout;
        m_searchEdit = new QLineEdit(this);
        m_searchEdit->setPlaceholderText("输入搜索关键词（支持 | 分隔多关键词）");
        m_searchEdit->setMinimumHeight(30);
        m_searchBtn  = new QPushButton("搜索", this);
        m_searchBtn->setFixedWidth(60);
        searchRow->addWidget(m_searchEdit);
        searchRow->addWidget(m_searchBtn);
        root->addLayout(searchRow);

        // ---- 命中统计 ----
        m_resultLabel = new QLabel("", this);
        root->addWidget(m_resultLabel);

        // ---- 字段列表 ----
        m_hint = new QLabel("发现以下数值字段（多选后点击「添加到图表」）：", this);
        m_hint->setVisible(false);
        root->addWidget(m_hint);

        m_fieldList = new QListWidget(this);
        m_fieldList->setSelectionMode(QAbstractItemView::MultiSelection);
        m_fieldList->setVisible(false);
        m_fieldList->setMaximumHeight(150);
        root->addWidget(m_fieldList);

        // ---- 按钮行 ----
        auto *btnRow = new QHBoxLayout;
        m_clearAllBtn = new QPushButton("清除所有图表", this);
        m_addBtn = new QPushButton("添加到图表", this);
        m_addBtn->setEnabled(false);
        btnRow->addStretch();
        btnRow->addWidget(m_clearAllBtn);
        btnRow->addWidget(m_addBtn);
        root->addLayout(btnRow);

        // ---- 分隔线 ----
        auto *line = new QFrame(this);
        line->setFrameShape(QFrame::HLine);
        line->setFrameShadow(QFrame::Sunken);
        root->addWidget(line);

        // ---- 图表滚动区 ----
        m_scrollArea = new QScrollArea(this);
        m_scrollArea->setWidgetResizable(true);
        m_scrollArea->setFrameShape(QFrame::NoFrame);

        m_gridContainer = new QWidget;
        m_gridLayout = new QGridLayout(m_gridContainer);
        m_gridLayout->setContentsMargins(4, 4, 4, 4);
        m_gridLayout->setSpacing(8);
        m_scrollArea->setWidget(m_gridContainer);
        root->addWidget(m_scrollArea, 1);

        // ---- 连接 ----
        connect(m_searchBtn,  &QPushButton::clicked,
                this, &DynamicChartManager::doSearch);
        connect(m_searchEdit, &QLineEdit::returnPressed,
                this, &DynamicChartManager::doSearch);
        connect(m_addBtn,     &QPushButton::clicked,
                this, &DynamicChartManager::addSelectedToChart);
        connect(m_clearAllBtn, &QPushButton::clicked, this, [this](){
            for (auto *card : m_cards) card->deleteLater();
            m_cards.clear();
            rebuildGrid();
        });
        connect(m_fieldList,  &QListWidget::itemSelectionChanged, this, [this](){
            m_addBtn->setEnabled(!m_fieldList->selectedItems().isEmpty());
        });
    }

    // 外部直接添加一张带数据的图表（供 offerChartFromSearchResults 使用）
    void addChartDirect(const QString &title,
                        const QStringList &fieldKeys,
                        const QVector<QVector<double>> &valuesPerKey,
                        const QVector<QVector<QDateTime>> &timestampsPerKey,
                        const QStringList &seriesLabels)
    {
        auto *card = createCard(title);
        for (int i = 0; i < fieldKeys.size(); ++i) {
            if (i < valuesPerKey.size() && i < timestampsPerKey.size()
                    && i < seriesLabels.size()) {
                card->chart()->addSeries(seriesLabels[i],
                                         valuesPerKey[i],
                                         timestampsPerKey[i]);
            }
        }
        rebuildGrid();
    }

private slots:
    void doSearch()
    {
        m_matchedLines.clear();
        m_fieldList->clear();
        m_fieldList->setVisible(false);
        m_hint->setVisible(false);
        m_addBtn->setEnabled(false);

        QString raw = m_searchEdit->text().trimmed();
        if (raw.isEmpty()) {
            m_resultLabel->setText("请输入搜索关键词");
            return;
        }

        QStringList keywords = raw.split('|', Qt::SkipEmptyParts);
        for (auto &k : keywords) k = k.trimmed();
        keywords.removeAll("");

        for (int i = 0; i < m_logLines.size(); ++i) {
            const QString &line = m_logLines[i];
            bool allMatch = true;
            for (const QString &kw : keywords) {
                if (!line.contains(kw, Qt::CaseInsensitive)) { allMatch = false; break; }
            }
            if (allMatch) m_matchedLines.append(i);
        }

        m_resultLabel->setText(QString("命中 %1 行").arg(m_matchedLines.size()));
        if (m_matchedLines.isEmpty()) return;

        QRegularExpression kvRx(
            R"(\b([A-Za-z_][A-Za-z0-9_]*)\s*[:=]\s*-?\d[\d.]*)"
            R"(|\b([A-Za-z_][A-Za-z0-9_]*)\s+(-?\d[\d.]*)(?!\w))"
        );
        QMap<QString, int> keyCount;

        for (int idx : m_matchedLines) {
            const QString &line = m_logLines[idx];
            QSet<QString> seenInLine;
            auto it = kvRx.globalMatch(line);
            while (it.hasNext()) {
                auto m = it.next();
                QString k = m.captured(1).isEmpty() ? m.captured(2) : m.captured(1);
                if (k.isEmpty()) continue;
                if (!seenInLine.contains(k)) {
                    keyCount[k]++;
                    seenInLine.insert(k);
                }
            }
        }

        if (keyCount.isEmpty()) {
            m_resultLabel->setText(
                QString("命中 %1 行（未发现数值键值对）").arg(m_matchedLines.size()));
            return;
        }

        QStringList keys = keyCount.keys();
        std::sort(keys.begin(), keys.end(), [&](const QString &a, const QString &b){
            return keyCount[a] > keyCount[b];
        });

        for (const QString &k : keys) {
            auto *item = new QListWidgetItem(
                QString("%1  （%2 行）").arg(k).arg(keyCount[k]), m_fieldList);
            item->setData(Qt::UserRole, k);
        }

        m_resultLabel->setText(QString("命中 %1 行，发现 %2 个数值字段")
                                   .arg(m_matchedLines.size()).arg(keys.size()));
        m_hint->setVisible(true);
        m_fieldList->setVisible(true);
    }

    void addSelectedToChart()
    {
        QStringList selectedKeys;
        for (auto *item : m_fieldList->selectedItems())
            selectedKeys << item->data(Qt::UserRole).toString();
        if (selectedKeys.isEmpty()) return;

        QRegularExpression tsRx(
            R"(\[\d+(?:\.\d+)?\s+(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})\])");
        const QString searchLabel = m_searchEdit->text().trimmed();

        // 提取所有选中字段的数据
        QVector<double>    combinedValues;
        QVector<QDateTime> combinedTimestamps;
        bool anyAdded = false;

        // 一次搜索 → 一张新图表，包含所有选中字段的系列
        QString cardTitle = selectedKeys.size() == 1
            ? QString("%1 (%2)").arg(selectedKeys.first(), searchLabel)
            : QString("%1 等 %2 项 (%3)")
                  .arg(selectedKeys.first())
                  .arg(selectedKeys.size())
                  .arg(searchLabel);

        auto *card = createCard(cardTitle);

        for (const QString &fieldKey : selectedKeys) {
            QRegularExpression valRxColon(
                QString(R"(\b%1\s*[:=]\s*([-\d.]+))").arg(
                    QRegularExpression::escape(fieldKey)));
            QRegularExpression valRxSpace(
                QString(R"(\b%1\s+([-\d.]+)(?!\w))").arg(
                    QRegularExpression::escape(fieldKey)));

            QVector<double>    values;
            QVector<QDateTime> timestamps;

            for (int idx : m_matchedLines) {
                const QString &line = m_logLines[idx];
                auto tsM  = tsRx.match(line);
                auto valM = valRxColon.match(line);
                if (!valM.hasMatch()) valM = valRxSpace.match(line);
                if (!tsM.hasMatch() || !valM.hasMatch()) continue;
                QDateTime dt = QDateTime::fromString(
                    tsM.captured(1), "yyyy-MM-dd HH:mm:ss");
                bool ok = false;
                double v = valM.captured(1).toDouble(&ok);
                if (!dt.isValid() || !ok) continue;
                timestamps.append(dt);
                values.append(v);
            }

            if (values.isEmpty()) continue;
            card->chart()->addSeries(
                QString("%1 (%2)").arg(fieldKey, searchLabel),
                values, timestamps);
            anyAdded = true;
        }

        if (!anyAdded) {
            // 没有数据，移除刚创建的空卡片
            m_cards.removeAll(card);
            card->deleteLater();
        }

        rebuildGrid();
    }

private:
    // 创建一张新卡片并注册
    ChartCard *createCard(const QString &title)
    {
        auto *card = new ChartCard(title, m_gridContainer);
        card->setMinimumHeight(280);
        m_cards.append(card);
        connect(card, &ChartCard::closeRequested, this, [this, card](){
            m_cards.removeAll(card);
            card->deleteLater();
            rebuildGrid();
        });
        return card;
    }

    // 重新排列所有卡片到网格
    void rebuildGrid()
    {
        // 先从布局中移除所有 item（不删除 widget）
        while (m_gridLayout->count() > 0) {
            QLayoutItem *item = m_gridLayout->takeAt(0);
            delete item;
        }

        const int n = m_cards.size();
        if (n == 0) return;

        const int cols = (n == 1) ? 1 : 2;

        for (int i = 0; i < n; ++i) {
            int row = i / cols;
            int col = i % cols;

            // 1张时设置列 span=1，居中 → 用 colspan 1 配合 setColumnStretch
            m_gridLayout->addWidget(m_cards[i], row, col);
            // 每张卡片高度固定
            m_cards[i]->setMinimumHeight(280);
            m_cards[i]->show();
        }

        // 列拉伸均等
        for (int c = 0; c < cols; ++c)
            m_gridLayout->setColumnStretch(c, 1);

        // 奇数张时，最后一行只有左列有内容，右列留空 → 拉伸已处理，视觉上左右各半
        // 1张时 cols=1，占满全宽

        // 行拉伸：给最后一行之后加 stretch，防止卡片被撑高
        // （留给 QScrollArea 自己处理高度）
    }

    const QStringList   &m_logLines;

    QLineEdit   *m_searchEdit   = nullptr;
    QPushButton *m_searchBtn    = nullptr;
    QLabel      *m_resultLabel  = nullptr;
    QLabel      *m_hint         = nullptr;
    QListWidget *m_fieldList    = nullptr;
    QPushButton *m_addBtn       = nullptr;
    QPushButton *m_clearAllBtn  = nullptr;

    QScrollArea *m_scrollArea    = nullptr;
    QWidget     *m_gridContainer = nullptr;
    QGridLayout *m_gridLayout    = nullptr;

    QList<ChartCard *> m_cards;
    QList<int>         m_matchedLines;
};

#endif // DYNAMICCHARTMANAGER_H
