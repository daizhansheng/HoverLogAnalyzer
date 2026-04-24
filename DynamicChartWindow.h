#ifndef DYNAMICCHARTWINDOW_H
#define DYNAMICCHARTWINDOW_H

#include <QWidget>
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
#include <algorithm>
#include "PressAnalyzer.h"
#include "DynamicChartWidget.h"

// ============================================================
// DynamicChartWindow
//
// Independent floating window containing its own DynamicChartWidget
// plus a built-in search UI for finding and plotting numeric fields
// from log lines.
//
// Each click of chartSearchButton creates a new independent instance.
// Windows do not share data with each other or with the embedded chart.
//
// Usage:
//   auto *win = new DynamicChartWindow(allLogLines, parent);
//   win->setAttribute(Qt::WA_DeleteOnClose);
//   win->setWindowTitle(QString("动态图表 #%1").arg(++m_dynamicChartCounter));
//   win->show();
// ============================================================
class DynamicChartWindow : public QWidget {
    Q_OBJECT
public:
    explicit DynamicChartWindow(const QStringList &logLines,
                                QWidget            *parent = nullptr)
        : QWidget(parent, Qt::Window)
        , m_logLines(logLines)
    {
        setMinimumSize(700, 600);
        resize(820, 680);
        setAttribute(Qt::WA_DeleteOnClose, false);  // caller sets if desired

        auto *root = new QVBoxLayout(this);
        root->setSpacing(6);
        root->setContentsMargins(8, 8, 8, 8);

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
        m_fieldList->setMaximumHeight(160);
        root->addWidget(m_fieldList);

        // ---- button row ----
        auto *btnRow = new QHBoxLayout;
        btnRow->setSpacing(6);
        m_addBtn   = new QPushButton("添加到图表", this);
        m_addBtn->setEnabled(false);
        m_clearBtn = new QPushButton("清除图表", this);
        btnRow->addWidget(m_addBtn, 1);
        btnRow->addWidget(m_clearBtn, 1);
        root->addLayout(btnRow);

        // ---- chart widget (takes remaining space) ----
        m_chart = new DynamicChartWidget(this);
        m_chart->setMinimumHeight(300);
        root->addWidget(m_chart, 1);

        // ---- connections ----
        connect(m_searchBtn,  &QPushButton::clicked,
                this, &DynamicChartWindow::doSearch);
        connect(m_searchEdit, &QLineEdit::returnPressed,
                this, &DynamicChartWindow::doSearch);
        connect(m_addBtn,     &QPushButton::clicked,
                this, &DynamicChartWindow::addSelectedToChart);
        connect(m_clearBtn,   &QPushButton::clicked,
                this, [this](){ m_chart->clearSeries(); });
        connect(m_fieldList,  &QListWidget::itemSelectionChanged, this, [this](){
            m_addBtn->setEnabled(!m_fieldList->selectedItems().isEmpty());
        });
    }

    // 外部直接添加数据系列（供 offerChartFromSearchResults 使用）
    void addSeriesDirect(const QString &name,
                         const QVector<double> &values,
                         const QVector<QDateTime> &timestamps)
    {
        m_chart->addSeries(name, values, timestamps);
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
        QStringList selectedKeys;
        for (auto *item : m_fieldList->selectedItems())
            selectedKeys << item->data(Qt::UserRole).toString();
        if (selectedKeys.isEmpty()) return;

        QRegularExpression tsRx(R"(\[\d+(?:\.\d+)?\s+(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})\])");
        const QString searchLabel = m_searchEdit->text().trimmed();

        for (const QString &fieldKey : selectedKeys) {
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
                auto tsM  = tsRx.match(line);
                auto valM = valRxColon.match(line);
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
        }
    }

private:
    const QStringList  &m_logLines;
    DynamicChartWidget *m_chart       = nullptr;

    QLineEdit   *m_searchEdit   = nullptr;
    QPushButton *m_searchBtn    = nullptr;
    QLabel      *m_resultLabel  = nullptr;
    QLabel      *m_hint         = nullptr;
    QListWidget *m_fieldList    = nullptr;
    QPushButton *m_addBtn       = nullptr;
    QPushButton *m_clearBtn     = nullptr;

    QList<int>   m_matchedLines;
};

#endif // DYNAMICCHARTWINDOW_H
