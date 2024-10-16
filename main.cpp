#include <QCoreApplication>
#include "packagemanager.h"

int main(int argc, char *argv[]) {
    QCoreApplication a(argc, argv);
    PackageManager manager;

    if (argc < 2) {
        qDebug() << "Usage: opm <command> [package_name ...]";
        return 1;
    }

    QString command = argv[1];
    QStringList packageNames;

    for (int i = 2; i < argc; ++i) {
        packageNames << argv[i];
    }

    if (command == "selfinstall") {
        manager.selfInstall();
    } else if (command == "selfupdate") {
        manager.selfUpdate();
    } else if (command == "update") {
        manager.update();
    } else if (command == "list") {
        manager.list();
    } else if (command == "install") {
        manager.install(packageNames);
    } else if (command == "remove") {
        manager.remove(packageNames);
    } else if (command == "upgrade") {
        manager.upgrade();
    } else if (command == "help") {
        manager.help();
    } else {
        qDebug() << "Unknown command or missing arguments.\nTry running opm help";
    }

    return 0;
}
