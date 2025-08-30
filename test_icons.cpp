#include <QApplication>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>
#include <QIcon>
#include <QLabel>
#include <QDebug>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QWidget window;
    window.setWindowTitle("图标测试");
    window.resize(400, 300);

    QVBoxLayout *layout = new QVBoxLayout(&window);

    // 测试图标加载
    QIcon folderIcon(":/icons/folder-open.png");
    QIcon fileIcon(":/icons/file-open.png");
    QIcon saveIcon(":/icons/save.png");
    QIcon clearIcon(":/icons/clear.png");
    QIcon searchIcon(":/icons/search.png");

    // 创建按钮
    QPushButton *btn1 = new QPushButton("文件夹", &window);
    btn1->setIcon(folderIcon);
    btn1->setIconSize(QSize(24, 24));

    QPushButton *btn2 = new QPushButton("文件", &window);
    btn2->setIcon(fileIcon);
    btn2->setIconSize(QSize(24, 24));

    QPushButton *btn3 = new QPushButton("保存", &window);
    btn3->setIcon(saveIcon);
    btn3->setIconSize(QSize(24, 24));

    QPushButton *btn4 = new QPushButton("清除", &window);
    btn4->setIcon(clearIcon);
    btn4->setIconSize(QSize(24, 24));

    QPushButton *btn5 = new QPushButton("搜索", &window);
    btn5->setIcon(searchIcon);
    btn5->setIconSize(QSize(24, 24));

    // 添加状态标签
    QLabel *statusLabel = new QLabel(&window);
    statusLabel->setText("图标加载状态:");

    // 检查图标是否有效
    QString status = "文件夹图标: " + (folderIcon.isNull() ? "失败" : "成功") + "\n";
    status += "文件图标: " + (fileIcon.isNull() ? "失败" : "成功") + "\n";
    status += "保存图标: " + (saveIcon.isNull() ? "失败" : "成功") + "\n";
    status += "清除图标: " + (clearIcon.isNull() ? "失败" : "成功") + "\n";
    status += "搜索图标: " + (searchIcon.isNull() ? "失败" : "成功");

    statusLabel->setText(status);

    // 添加到布局
    layout->addWidget(statusLabel);
    layout->addWidget(btn1);
    layout->addWidget(btn2);
    layout->addWidget(btn3);
    layout->addWidget(btn4);
    layout->addWidget(btn5);

    window.show();

    return app.exec();
}



