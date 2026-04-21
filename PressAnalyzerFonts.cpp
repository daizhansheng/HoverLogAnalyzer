// PressAnalyzerFonts.cpp - Font settings functions
#include "PressAnalyzer.h"
#include <QFontDialog>
#include <QMessageBox>
#include <QSettings>

// ==================== 字体设置功能 ====================

void PressAnalyzer::setLogFont()
{
    bool ok;
    QFont font = QFontDialog::getFont(&ok, currentLogFont, this, "设置日志字体");

    if (ok) {
        currentLogFont = font;
        logFontPointSize = font.pointSize();
        // 更新跨窗口共享字体
        s_sharedLogFont = font;
        s_sharedLogFontSet = true;

        // 应用到日志视图
        if (logView) {
            logView->setFont(font);
            logView->document()->setDefaultFont(font);  // 显式同步文档默认字体
        }

        // 应用到搜索结果视图
        if (searchResultView) {
            searchResultView->setFont(font);
        }

        // 应用到搜索框
        if (searchCombo) {
            searchCombo->setFont(font);
        }

        // 保存字体设置（日志字体不持久化，启动始终使用 Menlo 11pt）
        // settings.setValue("fonts/logFont", font);

        QMessageBox::information(this, "字体设置", "日志字体已更新！");
    }
}

void PressAnalyzer::setChartFont()
{
    bool ok;
    QFont font = QFontDialog::getFont(&ok, currentChartFont, this, "设置图表字体");

    if (ok) {
        currentChartFont = font;

        // 更新图表字体主题
        ChartStyleManager::FontTheme fontTheme;
        fontTheme.title = QFont(font.family(), font.pointSize() + 2, QFont::Bold);
        fontTheme.axis = QFont(font.family(), font.pointSize() - 3);
        fontTheme.label = QFont(font.family(), font.pointSize() - 4);
        fontTheme.tooltip = QFont(font.family(), font.pointSize() - 3);

        ChartStyleManager::setFontTheme(fontTheme);

        // 触发图表重绘
        if (batteryChart) {
            batteryChart->update();
        }
        if (socChart) {
            socChart->update();
        }
        if (usageChart) {
            usageChart->update();
        }
        if (cameraTempChart) {
            cameraTempChart->update();
        }

        // 保存字体设置
        QSettings settings("ZZTools", "HoverLogAnalyzer");
        settings.setValue("fonts/chartFont", font);

        QMessageBox::information(this, "字体设置", "图表字体已更新！");
    }
}

void PressAnalyzer::setEventFont()
{
    bool ok;
    QFont font = QFontDialog::getFont(&ok, currentEventFont, this, "设置事件列表字体");

    if (ok) {
        currentEventFont = font;

        // 应用到到主事件列表
        if (eventList) {
            eventList->setFont(font);
        }

        // 应用到相机事件列表
        if (cameraEventList) {
            cameraEventList->setFont(font);
        }

        // 应用到心跳丢失事件列表
        if (heartbeatLostEventList) {
            heartbeatLostEventList->setFont(font);
        }

        // 保存字体设置
        QSettings settings("ZZTools", "HoverLogAnalyzer");
        settings.setValue("fonts/eventFont", font);

        QMessageBox::information(this, "字体设置", "事件列表字体已更新！");
    }
}

void PressAnalyzer::resetAllFonts()
{
    // 重置日志字体：macOS=Menlo, Windows=Consolas, Linux=DejaVu Sans Mono
    currentLogFont = platformMonoFont(11);
    logFontPointSize = 11;
    // 重置共享字体
    s_sharedLogFont = currentLogFont;
    s_sharedLogFontSet = false;

    if (logView) {
        logView->setFont(currentLogFont);
        logView->document()->setDefaultFont(currentLogFont);
    }
    if (searchResultView) {
        searchResultView->setFont(currentLogFont);
    }
    if (searchCombo) {
        searchCombo->setFont(currentLogFont);
    }

    // 重置事件列表字体：macOS=Courier New 11pt，Windows=Segoe UI 9pt
    currentEventFont = QFont(
#if defined(Q_OS_WIN)
        "Segoe UI", 9
#else
        "Courier New", 11
#endif
    );
    if (eventList) {
        eventList->setFont(currentEventFont);
    }
    if (cameraEventList) {
        cameraEventList->setFont(currentEventFont);
    }
    if (heartbeatLostEventList) {
        heartbeatLostEventList->setFont(currentEventFont);
    }

    // 重置图表字体：FontTheme 默认构造已根据平台自动选择字体（Windows=Segoe UI, macOS/Linux=Arial）
    ChartStyleManager::FontTheme fontTheme;
    currentChartFont = fontTheme.title;
    currentChartFont.setPointSize(12);

    ChartStyleManager::setFontTheme(fontTheme);

    // 触发图表重绘
    if (batteryChart) {
        batteryChart->update();
    }
    if (socChart) {
        socChart->update();
    }
    if (usageChart) {
        usageChart->update();
    }
    if (cameraTempChart) {
        cameraTempChart->update();
    }

    // 清除保存的字体设置
    QSettings settings("ZZTools", "HoverLogAnalyzer");
    settings.remove("fonts/logFont");
    settings.remove("fonts/eventFont");
    settings.remove("fonts/chartFont");

    QMessageBox::information(this, "字体重置", "所有字体已重置为默认值！");
}

void PressAnalyzer::applySavedFonts()
{
    // 应用日志字体
    if (logView) {
        logView->setFont(currentLogFont);
        logView->document()->setDefaultFont(currentLogFont);
    }
    if (searchResultView) {
        searchResultView->setFont(currentLogFont);
    }
    if (searchCombo) {
        searchCombo->setFont(currentLogFont);
    }

    // 应用事件列表字体
    if (eventList) {
        eventList->setFont(currentEventFont);
    }
    if (cameraEventList) {
        cameraEventList->setFont(currentEventFont);
    }
    if (heartbeatLostEventList) {
        heartbeatLostEventList->setFont(currentEventFont);
    }

    // 应用图表字体
    ChartStyleManager::FontTheme fontTheme;
    fontTheme.title = QFont(currentChartFont.family(), currentChartFont.pointSize() + 2, QFont::Bold);
    fontTheme.axis = QFont(currentChartFont.family(), currentChartFont.pointSize() - 3);
    fontTheme.label = QFont(currentChartFont.family(), currentChartFont.pointSize() - 4);
    fontTheme.tooltip = QFont(currentChartFont.family(), currentChartFont.pointSize() - 3);

    ChartStyleManager::setFontTheme(fontTheme);

    // 触发图表重绘
    if (batteryChart) {
        batteryChart->update();
    }
    if (socChart) {
        socChart->update();
    }
    if (usageChart) {
        usageChart->update();
    }
    if (cameraTempChart) {
        cameraTempChart->update();
    }
}
