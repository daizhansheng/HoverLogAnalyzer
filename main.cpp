#include "PressAnalyzer.h"

#include <QApplication>

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
    return a.exec();
}
