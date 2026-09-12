#include <QApplication>
#include "main_window.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("CameraProcessor");
    app.setOrganizationName("CustomCamera");

    MainWindow window;
    window.show();

    return app.exec();
}
