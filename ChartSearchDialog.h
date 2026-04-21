#ifndef CHARTSEARCHDIALOG_H
#define CHARTSEARCHDIALOG_H

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QListWidget>
#include <QLabel>
#include <QStringList>
#include <QVector>
#include <QDateTime>
#include <QMap>
#include <QSet>
#include <QRegularExpression>
#include <QDockWidget>
#include <algorithm>
#include "DynamicChartWidget.h"
#include "PressAnalyzer.h"

// ============================================================
// ChartSearchDialog
//
// Standalone dialog for searching log lines and adding numeric
// key=value / key: value fields to the dynamic chart.
//
// Usage:
//   auto *dlg = new ChartSearchDialog(allLogLines, dynamicChart, dynamicChartDock, parent);
//   dlg->setAttribute(Qt::WA_DeleteOnClose);
//   dlg->show();
// ============================================================
class ChartSearchDialog : public QDialog {
    Q_OBJECT
public:
    explicit ChartSearchDialog(const QStringList &logLines,
                               DynamicChartWidget *chart,
                               QDockWidget        *chartDock,
                               QWidget            *parent = nullptr)
        : QDialog(parent)
        , m_logLines(logLines)
        , m_chart(chart)
        , m_chartDock(chartDock)
    {
        setWindowTitle("动态图表搜索");
        setMinimumSize(500, 560);
        resize(520, 580);
        setAttribute(Qt::WA_DeleteOnClose, false);  // caller sets this if desired

        auto *root = new QVBoxLayout(this);
        root->setSpacing(6);
        root->setContentsMargins(10, 10, 10, 10);

        // ---- search row ----
        auto *searchRow = new QHBoxLayout;
        m_searchEdit = new QLineEdit(this);
        m_searchEdit->setPlaceholderText("输入搜索关键词（支持 | 分隔多关键词）");
        m_searchEdit->setMinimumHeight(platformPx(30));
        m_searchBtn  = new QPushButton("搜索", this);
        m_searchBtn->setFixedWidth(platformPx(60));
        searchRow->addWidget(m_searchEdit);
        searchRow->addWidget(m_searchBtn);
        root->addLayout(searchRow);

        // ---- result count label ----
        m_resultLabel = new QLabel("", this);
        root->addWidget(m_resultLabel);

        // ---- key-value field list ----
        m_hint = new QLabel("发现以下数值字段（多选后点击「添加到图表」）：", this);
        m_hint->setVisible(false);
        root->addWidget(m_hint);

        m_fieldList = new QListWidget(this);
        m_fieldList->setSelectionMode(QAbstractItemView::MultiSelection);
        m_fieldList->setVisible(false);
        m_fieldList->setMinimumHeight(200);
        root->addWidget(m_fieldList, 1);

        // ---- button row ----
        auto *btnRow = new QHBoxLayout;
        m_addBtn    = new QPushButton("添加到图表", this);
        m_addBtn->setEnabled(false);
        m_closeBtn  = new QPushButton("关闭", this);
        btnRow->addStretch();
        btnRow->addWidget(m_addBtn);
        btnRow->addWidget(m_closeBtn);
        root->addLayout(btnRow);

        // ---- connections ----
        connect(m_searchBtn,  &QPushButton::clicked,   this, &ChartSearchDialog::doSearch);
        connect(m_searchEdit, &QLineEdit::returnPressed, this, &ChartSearchDialog::doSearch);
        connect(m_addBtn,     &QPushButton::clicked,   this, &ChartSearchDialog::addSelectedToChart);
        connect(m_closeBtn,   &QPushButton::clicked,   this, &QDialog::close);
        connect(m_fieldList,  &QListWidget::itemSelectionChanged, this, [this](){
            m_addBtn->setEnabled(!m_fieldList->selectedItems().isEmpty());
        });
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

        QString raw = m_searchEdit->text().trimmed();
        if (raw.isEmpty()) {
            m_resultLabel->setText("请输入搜索关键词");
            return;
        }

        // Build keyword list (split by |)
        QStringList keywords = raw.split('|', Qt::SkipEmptyParts);
        for (auto &k : keywords) k = k.trimmed();
        keywords.removeAll("");

        // Search all log lines
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

        // Scan matched lines for numeric key=value / key: value / key value pairs
        // Pattern 1: key=value or key: value  (e.g. "temp=36.5", "cpu: 12.3")
        // Pattern 2: key value  (e.g. "temp 36.5" — space-separated, word boundary)
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
                // Group 1 = key from "key=val" or "key: val"; Group 2 = key from "key val"
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
        {
            int headerIdx = -1;

            // Step 1: Find a header line among matched lines
            for (int idx : m_matchedLines) {
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

            // Fallback: search backwards from first matched line
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

                for (int c = 0; c < headerTokens.size(); ++c) {
                    if (dataCount > 0 && numericHits[c] * 2 >= dataCount)
                        m_csvNumericColIndices.append(c);
                }

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
            // Sort by frequency descending
            QStringList keys = keyCount.keys();
            std::sort(keys.begin(), keys.end(), [&](const QString &a, const QString &b){
                return keyCount[a] > keyCount[b];
            });

            // Populate field list
            for (const QString &k : keys) {
                auto *item = new QListWidgetItem(
                    QString("%1  （%2 行）").arg(k).arg(keyCount[k]),
                    m_fieldList);
                item->setData(Qt::UserRole, k);
            }

            m_resultLabel->setText(QString("命中 %1 行，发现 %2 个匹配数值字段")
                                       .arg(m_matchedLines.size())
                                       .arg(keys.size()));
            m_hint->setVisible(true);
            m_fieldList->setVisible(true);
            return;
        }

        m_resultLabel->setText(
            QString("命中 %1 行（未发现匹配的数值字段）").arg(m_matchedLines.size()));
    }

    void addSelectedToChart()
    {
        if (!m_chart) return;

        QStringList kvKeys;
        QList<int>  csvColIndices;
        QStringList csvColNames;

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

        QRegularExpression tsRx(R"(\[\d+(?:\.\d+)?\s+(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})\])");
        const QString searchLabel = m_searchEdit->text().trimmed();

        bool anyAdded = false;

        // ---- Handle key=value fields ----
        for (const QString &fieldKey : kvKeys) {
            QRegularExpression valRxColon(
                QString(R"(\b%1\s*[:=]\s*([-\d.]+))").arg(QRegularExpression::escape(fieldKey))
            );
            QRegularExpression valRxSpace(
                QString(R"(\b%1\s+([-\d.]+)(?!\w))").arg(QRegularExpression::escape(fieldKey))
            );
            QVector<double>    values;
            QVector<QDateTime> timestamps;

            for (int idx : m_matchedLines) {
                const QString &line = m_logLines[idx];
                auto tsM   = tsRx.match(line);
                auto valM  = valRxColon.match(line);
                if (!valM.hasMatch()) valM = valRxSpace.match(line);
                if (!tsM.hasMatch() || !valM.hasMatch()) continue;
                QDateTime dt = QDateTime::fromString(tsM.captured(1), "yyyy-MM-dd HH:mm:ss");
                bool ok = false;
                double v = valM.captured(1).toDouble(&ok);
                if (!dt.isValid() || !ok) continue;
                timestamps.append(dt);
                values.append(v);
            }

            if (values.isEmpty()) continue;
            m_chart->addSeries(
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
            m_chart->addSeries(
                QString("%1 (%2)").arg(colName, searchLabel),
                values, timestamps);
            anyAdded = true;
        }

        if (anyAdded && m_chartDock) {
            m_chartDock->show();
            m_chartDock->raise();
        }
    }

private:
    // ---- CSV helpers ----
    static QStringList csvParseTokens(const QString &line)
    {
        if (line.count(',') < 2) return {};
        QStringList tokens = line.split(',');
        if (tokens.size() < 3) return {};
        QString &first = tokens[0];
        int pos = first.lastIndexOf(": ");
        if (pos >= 0) first = first.mid(pos + 2);
        for (auto &t : tokens) t = t.trimmed();
        return tokens;
    }

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

    const QStringList  &m_logLines;
    DynamicChartWidget *m_chart     = nullptr;
    QDockWidget        *m_chartDock = nullptr;

    QLineEdit   *m_searchEdit   = nullptr;
    QPushButton *m_searchBtn    = nullptr;
    QLabel      *m_resultLabel  = nullptr;
    QLabel      *m_hint         = nullptr;
    QListWidget *m_fieldList    = nullptr;
    QPushButton *m_addBtn       = nullptr;
    QPushButton *m_closeBtn     = nullptr;

    QList<int>   m_matchedLines;  // indices into m_logLines

    // CSV format state
    QStringList  m_csvColumnNames;
    int          m_csvHeaderLineIdx = -1;
    QVector<int> m_csvNumericColIndices;
};

#endif // CHARTSEARCHDIALOG_H
