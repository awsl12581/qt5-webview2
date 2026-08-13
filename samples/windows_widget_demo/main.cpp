#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);

    QWidget window;
    window.setWindowTitle(QStringLiteral("Qt 5 QWidget Demo"));
    window.resize(480, 240);

    auto* layout = new QVBoxLayout(&window);
    auto* label = new QLabel(QStringLiteral("Qt QWidget is running."), &window);
    auto* button = new QPushButton(QStringLiteral("Click me"), &window);
    layout->addWidget(label);
    layout->addWidget(button);
    layout->addStretch();

    QObject::connect(button, &QPushButton::clicked, &window, [label] {
        label->setText(QStringLiteral("The button click reached Qt."));
    });

    window.show();
    return application.exec();
}
