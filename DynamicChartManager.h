#ifndef DYNAMICCHARTMANAGER_H
#define DYNAMICCHARTMANAGER_H

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QScrollArea>
#include <QLineEdit>
#include <QComboBox>
#include <QAbstractItemView>
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
#include "PressAnalyzer.h"

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
        titleBar->setFixedHeight(platformPx(28));
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
#if defined(Q_OS_WIN)
        m_titleLabel->setStyleSheet("font-size:9pt; font-weight:bold; color:#1F2D3D; background:transparent;");
#else
        m_titleLabel->setStyleSheet("font-size:12px; font-weight:bold; color:#1F2D3D; background:transparent;");
#endif
        titleLayout->addWidget(m_titleLabel, 1);

        auto *closeBtn = new QPushButton("✕", titleBar);
        closeBtn->setFixedSize(platformPx(20), platformPx(20));
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
// 嵌入侧边栏的动态图表管理器。
//
// 顶部：搜索栏 + 字段选择列表 + 「添加到图表」按钮
// 下方：可滚动区域，单列垂直排列图表卡片
// ============================================================
class DynamicChartManager : public QWidget {
    Q_OBJECT
public:
    explicit DynamicChartManager(const QStringList &logLines,
                                 QWidget            *parent = nullptr)
        : QWidget(parent)
        , m_logLines(logLines)
    {
        setAttribute(Qt::WA_DeleteOnClose, false);

        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(6, 6, 6, 6);
        root->setSpacing(4);

        // ---- 搜索行 ----
        auto *searchRow = new QHBoxLayout;
        m_searchEdit = new QComboBox(this);
        m_searchEdit->setEditable(true);
        m_searchEdit->lineEdit()->setPlaceholderText("搜索关键词（| 分隔多词）");
        m_searchEdit->setMinimumHeight(28);
        m_searchEdit->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        m_searchEdit->setMinimumContentsLength(10);
        // 下拉列表中长文本用省略号截断，避免 popup 超出组件宽度
        m_searchEdit->view()->setTextElideMode(Qt::ElideMiddle);
        m_searchEdit->view()->installEventFilter(this);
        m_searchBtn  = new QPushButton("搜索", this);
        m_searchBtn->setFixedWidth(platformPx(50));
        searchRow->addWidget(m_searchEdit, 1);
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
        m_fieldList->setMaximumHeight(120);
        root->addWidget(m_fieldList);

        // ---- 按钮行 ----
        auto *btnRow = new QHBoxLayout;
        btnRow->setSpacing(6);
        m_clearAllBtn = new QPushButton("清除所有图表", this);
        m_addBtn = new QPushButton("添加到图表", this);
        m_addBtn->setEnabled(false);
        btnRow->addWidget(m_clearAllBtn, 1);
        btnRow->addWidget(m_addBtn, 1);
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
        connect(m_searchEdit->lineEdit(), &QLineEdit::returnPressed,
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
        m_csvColumnNames.clear();
        m_csvHeaderLineIdx = -1;
        m_csvNumericColIndices.clear();

        QString raw = m_searchEdit->currentText().trimmed();
        if (raw.isEmpty()) {
            m_resultLabel->setText("请输入搜索关键词");
            return;
        }

        // 添加到搜索历史（不重复）
        int existIdx = m_searchEdit->findText(raw);
        if (existIdx >= 0) {
            // 已有，移到最前面
            m_searchEdit->removeItem(existIdx);
        }
        m_searchEdit->insertItem(0, raw);
        m_searchEdit->setCurrentIndex(0);
        // 限制历史条目数量
        while (m_searchEdit->count() > 20)
            m_searchEdit->removeItem(m_searchEdit->count() - 1);

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
                // 双向匹配：字段名包含关键词，或关键词包含字段名
                // 例如搜索 "core temp:" 时能匹配到字段 "temp"
                bool fieldMatchesKeyword = false;
                for (const QString &kw : keywords) {
                    if (k.contains(kw, Qt::CaseInsensitive)
                        || kw.contains(k, Qt::CaseInsensitive)) {
                        fieldMatchesKeyword = true;
                        break;
                    }
                }
                if (!fieldMatchesKeyword) continue;
                if (!seenInLine.contains(k)) {
                    keyCount[k]++;
                    seenInLine.insert(k);
                }
            }
        }

        // ===== CSV format detection (always runs, takes priority over key=value) =====
        // Detect "header + data" style CSV logs:
        //   Header: [I|Publisher]: Publish: vol(mV), current(mA), temp, SOC, ...
        //   Data:   [D|Publisher]: Publish:  7101,      914,     33.50, 20%, ...
        {
            int headerIdx = -1;

            // Step 1: Find a header line among matched lines
            for (int idx : m_matchedLines) {
                QStringList tokens = csvParseTokens(m_logLines[idx]);
                if (tokens.size() < 3) continue;
                int numericCount = 0;
                for (const QString &t : tokens)
                    if (csvIsNumericToken(t)) numericCount++;
                // Header: majority of tokens are non-numeric (column names)
                if (numericCount * 2 < tokens.size()) {
                    headerIdx = idx;
                    break;
                }
            }

            // Fallback: search backwards from first matched line (header may
            // use a different log level and not match the search keywords)
            if (headerIdx < 0 && !m_matchedLines.isEmpty()) {
                int start = qMax(0, m_matchedLines.first() - 50);
                for (int idx = m_matchedLines.first() - 1; idx >= start; --idx) {
                    QStringList tokens = csvParseTokens(m_logLines[idx]);
                    if (tokens.size() < 3) continue;
                    int numericCount = 0;
                    for (const QString &t : tokens)
                        if (csvIsNumericToken(t)) numericCount++;
                    if (numericCount * 2 < tokens.size()) {
                        headerIdx = idx;
                        break;
                    }
                }
            }

            if (headerIdx >= 0) {
                QStringList headerTokens = csvParseTokens(m_logLines[headerIdx]);
                m_csvHeaderLineIdx = headerIdx;
                m_csvColumnNames = headerTokens;

                // Determine numeric columns by scanning data lines
                QVector<int> numericHits(headerTokens.size(), 0);
                int dataCount = 0;

                for (int idx : m_matchedLines) {
                    if (idx == headerIdx) continue;
                    QStringList tokens = csvParseTokens(m_logLines[idx]);
                    if (tokens.size() != headerTokens.size()) continue;
                    dataCount++;
                    for (int c = 0; c < tokens.size(); ++c)
                        if (csvIsNumericToken(tokens[c])) numericHits[c]++;
                }

                // Column is numeric if >50% of data lines have numeric values
                for (int c = 0; c < headerTokens.size(); ++c) {
                    if (dataCount > 0 && numericHits[c] * 2 >= dataCount)
                        m_csvNumericColIndices.append(c);
                }

                // Add numeric CSV columns to field list
                for (int c : m_csvNumericColIndices) {
                    if (m_csvColumnNames[c].isEmpty()) continue;
                    auto *item = new QListWidgetItem(
                        QString("%1  （%2 行）").arg(m_csvColumnNames[c]).arg(dataCount),
                        m_fieldList);
                    item->setData(Qt::UserRole, QString("csv:%1").arg(c));
                    item->setData(Qt::UserRole + 1, m_csvColumnNames[c]);
                }
            }
        }

        // Priority: CSV results > key=value results > no results
        if (!m_csvNumericColIndices.isEmpty()) {
            m_resultLabel->setText(
                QString("命中 %1 行，发现 %2 个CSV数值列")
                    .arg(m_matchedLines.size())
                    .arg(m_csvNumericColIndices.size()));
            m_hint->setVisible(true);
            m_fieldList->setVisible(true);
            return;
        }

        if (!keyCount.isEmpty()) {
            QStringList keys = keyCount.keys();
            std::sort(keys.begin(), keys.end(), [&](const QString &a, const QString &b){
                return keyCount[a] > keyCount[b];
            });

            for (const QString &k : keys) {
                auto *item = new QListWidgetItem(
                    QString("%1  （%2 行）").arg(k).arg(keyCount[k]), m_fieldList);
                item->setData(Qt::UserRole, k);
            }

            m_resultLabel->setText(QString("命中 %1 行，发现 %2 个匹配数值字段")
                                       .arg(m_matchedLines.size()).arg(keys.size()));
            m_hint->setVisible(true);
            m_fieldList->setVisible(true);
            return;
        }

        m_resultLabel->setText(
            QString("命中 %1 行（未发现匹配的数值字段）").arg(m_matchedLines.size()));
    }

    void addSelectedToChart()
    {
        QStringList kvKeys;            // key=value field names
        QList<int>  csvColIndices;     // CSV column indices
        QStringList csvColNames;       // CSV column display names

        for (auto *item : m_fieldList->selectedItems()) {
            QString data = item->data(Qt::UserRole).toString();
            if (data.startsWith("csv:")) {
                csvColIndices.append(data.mid(4).toInt());
                csvColNames.append(item->data(Qt::UserRole + 1).toString());
            } else {
                kvKeys << data;
            }
        }
        if (kvKeys.isEmpty() && csvColIndices.isEmpty()) return;

        QRegularExpression tsRx(
            R"(\[\d+(?:\.\d+)?\s+(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})\])");
        const QString searchLabel = m_searchEdit->currentText().trimmed();

        // Build card title from all selected fields
        QStringList allNames = kvKeys + csvColNames;
        QString cardTitle = allNames.size() == 1
            ? QString("%1 (%2)").arg(allNames.first(), searchLabel)
            : QString("%1 等 %2 项 (%3)")
                  .arg(allNames.first())
                  .arg(allNames.size())
                  .arg(searchLabel);

        auto *card = createCard(cardTitle);
        bool anyAdded = false;

        // ---- Handle key=value fields ----
        for (const QString &fieldKey : kvKeys) {
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

        // ---- Handle CSV columns ----
        for (int i = 0; i < csvColIndices.size(); ++i) {
            int colIdx = csvColIndices[i];
            const QString &colName = csvColNames[i];

            QVector<double>    values;
            QVector<QDateTime> timestamps;

            for (int idx : m_matchedLines) {
                if (idx == m_csvHeaderLineIdx) continue;
                const QString &line = m_logLines[idx];

                auto tsM = tsRx.match(line);
                if (!tsM.hasMatch()) continue;
                QDateTime dt = QDateTime::fromString(
                    tsM.captured(1), "yyyy-MM-dd HH:mm:ss");
                if (!dt.isValid()) continue;

                QStringList tokens = csvParseTokens(line);
                if (colIdx >= tokens.size()) continue;

                QString valStr = tokens[colIdx];
                if (valStr.endsWith('%')) valStr.chop(1);
                bool ok;
                double v = valStr.toDouble(&ok);
                if (!ok) continue;

                timestamps.append(dt);
                values.append(v);
            }

            if (values.isEmpty()) continue;
            card->chart()->addSeries(
                QString("%1 (%2)").arg(colName, searchLabel),
                values, timestamps);
            anyAdded = true;
        }

        if (!anyAdded) {
            m_cards.removeAll(card);
            card->deleteLater();
        }

        rebuildGrid();
    }

protected:
    // 限制 QComboBox 下拉弹出框宽度不超过组件本身
    bool eventFilter(QObject *obj, QEvent *e) override
    {
        if (obj == m_searchEdit->view() && e->type() == QEvent::Show) {
            QWidget *popup = m_searchEdit->view()->parentWidget();
            if (popup) {
                popup->setFixedWidth(m_searchEdit->width());
            }
        }
        return QWidget::eventFilter(obj, e);
    }

private:
    // ---- CSV helpers ----
    // Parse comma-separated tokens from a log line.
    // Strips the log prefix and tag from the first element
    // (e.g. "[ts][I|Pub]: Publish: vol(mV)" → "vol(mV)").
    static QStringList csvParseTokens(const QString &line)
    {
        if (line.count(',') < 2) return {};
        QStringList tokens = line.split(',');
        if (tokens.size() < 3) return {};
        // Strip log prefix + tag from first token (everything up to last ": ")
        QString &first = tokens[0];
        int pos = first.lastIndexOf(": ");
        if (pos >= 0) first = first.mid(pos + 2);
        for (auto &t : tokens) t = t.trimmed();
        return tokens;
    }

    // Check if a token represents a numeric value.
    // Allows trailing '%', rejects hex (0x...) and pure strings.
    static bool csvIsNumericToken(const QString &token)
    {
        QString t = token.trimmed();
        if (t.isEmpty()) return false;
        if (t.endsWith('%')) t.chop(1);
        if (t.startsWith("0x", Qt::CaseInsensitive)) return false;
        bool ok;
        t.toDouble(&ok);
        return ok;
    }

    // 创建一张新卡片并注册
    ChartCard *createCard(const QString &title)
    {
        auto *card = new ChartCard(title, m_gridContainer);
        card->setMinimumHeight(240);
        m_cards.append(card);
        connect(card, &ChartCard::closeRequested, this, [this, card](){
            m_cards.removeAll(card);
            card->deleteLater();
            rebuildGrid();
        });
        return card;
    }

    // 重新排列所有卡片（单列垂直排列，适合侧边栏窄宽度）
    void rebuildGrid()
    {
        // 先从布局中移除所有 item（不删除 widget）
        while (m_gridLayout->count() > 0) {
            QLayoutItem *item = m_gridLayout->takeAt(0);
            delete item;
        }

        const int n = m_cards.size();
        if (n == 0) return;

        for (int i = 0; i < n; ++i) {
            m_gridLayout->addWidget(m_cards[i], i, 0);
            m_cards[i]->setMinimumHeight(240);
            m_cards[i]->show();
        }

        m_gridLayout->setColumnStretch(0, 1);
    }

    const QStringList   &m_logLines;

    QComboBox   *m_searchEdit   = nullptr;
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

    // CSV format state
    QStringList  m_csvColumnNames;         // column names from header
    int          m_csvHeaderLineIdx = -1;  // index of header line in m_logLines
    QVector<int> m_csvNumericColIndices;   // indices of numeric columns
};

#endif // DYNAMICCHARTMANAGER_H
