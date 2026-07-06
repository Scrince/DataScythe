#include "main_window.h"

#include <QApplication>
#include <QIcon>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("DataScythe");
    QApplication::setOrganizationName("DataScythe");
    QApplication::setWindowIcon(QIcon(":/icons/datascythe.png"));

    datascythe::gui::MainWindow window;
    window.show();

    return app.exec();
}
