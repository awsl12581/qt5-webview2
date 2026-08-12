#include "DemoWindow.h"

#include <QApplication>

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    samples::demo::DemoWindow window;
    window.show();
    return application.exec();
}
