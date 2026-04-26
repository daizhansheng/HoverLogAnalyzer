// ParseProgressDialog.h - 独立的解析进度窗口
//
// 一个独立的浮动窗口，用于在后台解析日志期间显示进度。
// 设计目标：
//   - 独立窗口（不嵌入主窗口工具栏/状态栏）
//   - 模态/非模态可选；默认非模态，避免阻塞用户其他操作
//   - 始终置顶，方便用户随时看到当前阶段
//   - 显示百分比进度条 + 当前阶段文案 + 累积阶段日志
//   - 调用 setProgress(percent, text)：percent < 0 表示"中途进度"，
//     仅刷新文案、保持进度条数值不变
//
// 用法：
//   ParseProgressDialog *dlg = new ParseProgressDialog(parentWindow);
//   dlg->show();
//   dlg->setProgress(0, "开始解析...");
//   ... worker progressChanged ...
//   dlg->setProgress(45, "正在解析 file.log");
//   ... 解析完成 ...
//   dlg->finishAndClose();
//
#ifndef PARSE_PROGRESS_DIALOG_H
#define PARSE_PROGRESS_DIALOG_H

#include <QDialog>
#include <QLabel>
#include <QProgressBar>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDateTime>
#include <QApplication>

// 无 Q_OBJECT：保持头文件 only，避免 moc 链接问题。
// 我们没有定义新信号/槽，仅复用 QDialog 已有的 close()。
class ParseProgressDialog : public QDialog
{
public:
    explicit ParseProgressDialog(QWidget *parent = nullptr)
        : QDialog(parent,
                  Qt::Dialog
                  | Qt::WindowTitleHint
                  | Qt::CustomizeWindowHint
                  | Qt::WindowStaysOnTopHint)
    {
        setWindowTitle(tr("日志解析进度"));
        setModal(false);
        setMinimumWidth(520);
        setMinimumHeight(220);

        QVBoxLayout *root = new QVBoxLayout(this);
        root->setContentsMargins(14, 14, 14, 12);
        root->setSpacing(8);

        m_stageLabel = new QLabel(tr("准备中..."), this);
        m_stageLabel->setWordWrap(true);
        QFont f = m_stageLabel->font();
        f.setBold(true);
        m_stageLabel->setFont(f);
        root->addWidget(m_stageLabel);

        m_progressBar = new QProgressBar(this);
        m_progressBar->setRange(0, 100);
        m_progressBar->setValue(0);
        m_progressBar->setTextVisible(true);
        m_progressBar->setFixedHeight(20);
        root->addWidget(m_progressBar);

        m_log = new QPlainTextEdit(this);
        m_log->setReadOnly(true);
        m_log->setMaximumBlockCount(500);   // 不让日志无限增长
        m_log->setStyleSheet(QStringLiteral(
            "QPlainTextEdit { background:#1e1e1e; color:#d4d4d4; "
            "border:1px solid #444; font-family:Menlo,Consolas,monospace; "
            "font-size:11px; }"));
        root->addWidget(m_log, 1);

        QHBoxLayout *btnRow = new QHBoxLayout;
        btnRow->addStretch(1);
        m_closeBtn = new QPushButton(tr("关闭"), this);
        m_closeBtn->setEnabled(false);  // 解析中禁用，完成后启用
        connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::close);
        btnRow->addWidget(m_closeBtn);
        root->addLayout(btnRow);
    }

    // percent: 0..100 表示进度；<0 表示仅更新文案，不改变进度条数值
    void setProgress(int percent, const QString &statusText)
    {
        if (percent >= 0) {
            m_progressBar->setValue(qBound(0, percent, 100));
        }
        if (!statusText.isEmpty()) {
            m_stageLabel->setText(statusText);
            appendLog(percent, statusText);
        }
        // 立即刷新 UI（解析槽本来就在主线程长时间持有 CPU）
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 5);
    }

    // 显式标记完成；启用关闭按钮，可由调用方再决定是否自动关闭
    void markFinished(const QString &finalText = QString())
    {
        m_progressBar->setValue(100);
        if (!finalText.isEmpty()) {
            m_stageLabel->setText(finalText);
            appendLog(100, finalText);
        } else {
            m_stageLabel->setText(tr("完成"));
        }
        m_closeBtn->setEnabled(true);
    }

    // 完成并立即关闭（用于无需用户确认的快速场景）
    void finishAndClose()
    {
        markFinished();
        close();
    }

protected:
    // 解析中禁止用户关闭；完成后才允许
    void closeEvent(QCloseEvent *e) override
    {
        if (!m_closeBtn->isEnabled()) {
            e->ignore();
            return;
        }
        QDialog::closeEvent(e);
    }

private:
    void appendLog(int percent, const QString &text)
    {
        const QString ts = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
        if (percent >= 0)
            m_log->appendPlainText(QStringLiteral("[%1] [%2%] %3").arg(ts).arg(percent).arg(text));
        else
            m_log->appendPlainText(QStringLiteral("[%1]      %2").arg(ts).arg(text));
    }

    QLabel         *m_stageLabel  = nullptr;
    QProgressBar   *m_progressBar = nullptr;
    QPlainTextEdit *m_log         = nullptr;
    QPushButton    *m_closeBtn    = nullptr;
};

#endif // PARSE_PROGRESS_DIALOG_H
