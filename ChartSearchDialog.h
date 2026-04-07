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
        m_searchEdit->setMinimumHeight(30);
        m_searchBtn  = new QPushButton("搜索", this);
        m_searchBtn->setFixedWidth(60);
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
                if (!seenInLine.contains(k)) {
                    keyCount[k]++;
                    seenInLine.insert(k);
                }
            }
        }

        if (keyCount.isEmpty()) {
            m_resultLabel->setText(QString("命中 %1 行（未发现数值键值对）").arg(m_matchedLines.size()));
            return;
        }

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

        m_resultLabel->setText(QString("命中 %1 行，发现 %2 个数值字段")
                                   .arg(m_matchedLines.size())
                                   .arg(keys.size()));
        m_hint->setVisible(true);
        m_fieldList->setVisible(true);
    }

    void addSelectedToChart()
    {
        if (!m_chart) return;

        QStringList selectedKeys;
        for (auto *item : m_fieldList->selectedItems())
            selectedKeys << item->data(Qt::UserRole).toString();
        if (selectedKeys.isEmpty()) return;

        QRegularExpression tsRx(R"(\[\d+(?:\.\d+)?\s+(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})\])");
        const QString searchLabel = m_searchEdit->text().trimmed();

        bool anyAdded = false;
        for (const QString &fieldKey : selectedKeys) {
            // Try both "key=val"/"key: val" and "key val" patterns
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

        if (anyAdded && m_chartDock) {
            m_chartDock->show();
            m_chartDock->raise();
        }
    }

private:
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
};

#endif // CHARTSEARCHDIALOG_H
