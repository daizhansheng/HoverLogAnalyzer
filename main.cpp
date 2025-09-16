#include "PressAnalyzer.h"

#include <QApplication>
#ifdef Q_OS_MAC
#include <QMenu>
#include <QAction>
#include <QWidget>
#include <QList>
#include <QIcon>
#include <QGuiApplication>
#include <QWindow>
#endif

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setWindowIcon(QIcon(":/new/image/logo.icns"));
    QFont font("Courier New");
    font.setStyleHint(QFont::Monospace); // 等宽
    font.setPointSize(11);                // 字体稍大，可根据需求调整
    font.setWeight(QFont::Medium);
    a.setFont(font);
    PressAnalyzer w;
    w.show();

#ifdef Q_OS_MAC
    // 在 macOS 的 Dock 栏右键菜单中显示应用打开的窗口
    static PressAnalyzer *g_lastActiveWindow = nullptr;
    QObject::connect(&a, &QApplication::focusChanged, [&](QWidget * /*old*/, QWidget *now){
        QWidget *top = now;
        while (top && top->parentWidget()) top = top->parentWidget();
        if (auto pa = qobject_cast<PressAnalyzer*>(top)) {
            g_lastActiveWindow = pa;
        }
    });

    QMenu *dockMenu = new QMenu(nullptr);
    QObject::connect(dockMenu, &QMenu::aboutToShow, [&]() {
        dockMenu->clear();
        // 列出所有已打开的 PressAnalyzer 窗口
        QList<PressAnalyzer*> windows;
        for (QWidget *tw : QApplication::topLevelWidgets()) {
            if (auto pa = qobject_cast<PressAnalyzer*>(tw)) {
                windows.append(pa);
            }
        }
        QWidget *active = QApplication::activeWindow();
        int idx = 1;
        for (PressAnalyzer *win : windows) {
            QString baseTitle = win->windowTitle().isEmpty() ? QString("窗口") : win->windowTitle();
            bool isFront = (win == active);
            if (!isFront && !active && g_lastActiveWindow == win) {
                // 应用在后台时，用最近一次前台窗口作为标记
                isFront = true;
            }
            QString stateTag;
            if (win->isMinimized()) stateTag = "（最小化）";
            else stateTag = isFront ? "（前台）" : "（后台）";
            QString label = QString("%1 #%2 %3").arg(baseTitle).arg(idx++).arg(stateTag);
            QAction *act = dockMenu->addAction(label);
            act->setCheckable(true);
            act->setChecked(isFront);
            QObject::connect(act, &QAction::triggered, [win]() {
                if (!win) return;
                if (win->isMinimized()) {
                    win->showNormal();
                } else {
                    win->show();
                }
                win->raise();
                win->activateWindow();
                QApplication::setActiveWindow(win);
                if (QWindow *w = win->windowHandle()) {
                    w->requestActivate();
                }
            });
        }
    });
    // 设置为 Dock 菜单
    dockMenu->setAsDockMenu();
#endif
    return a.exec();
}
