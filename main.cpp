#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QFile>
#include <QDir>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QDebug>
#include <QEventLoop>
#include <QStandardPaths>
#include <QSettings>
#include <QDirIterator>
#include <windows.h>
#include <shlobj.h> // Include for shortcut creation

class PackageManager {
public:
    PackageManager() {
        appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(appDataDir);

        manifestFile = appDataDir + "/manifest.json";
        installedPackagesFile = appDataDir + "/opm_installed_packages.json";

        parseInstalledPackages();
        loadManifest();
    }

    void help() {
        qDebug() << "+-----------------------------------------------------------------------------------------+";
        qDebug() << "|                                OdizinnePackageManager:                                  |";
        qDebug() << "|-----------------------------------------------------------------------------------------|";
        qDebug() << "|                                                                                         |";
        qDebug() << "|  update                   - Pull latest app manifest and check for available upgrades.  |";
        qDebug() << "|  list                     - List all available packages with their versions.            |";
        qDebug() << "|  install <package_names>  - Install one or more packages.                               |";
        qDebug() << "|  remove <package_names>   - Remove one or more installed packages.                      |";
        qDebug() << "|  upgrade                  - Upgrade installed packages to the latest versions.          |";
        qDebug() << "|  selfinstall              - Install opm in %localappdata%/programs/ and add to path.    |";
        qDebug() << "|  help                     - Display this help message.                                  |";
        qDebug() << "|                                                                                         |";
        qDebug() << "+-----------------------------------------------------------------------------------------+";
    }

    void selfinstall() {
        QString sourceDir = QCoreApplication::applicationDirPath();
        QString targetDir = QDir::homePath() + "/AppData/Local/Programs/opm";

        // Ensure the target directory exists
        QDir().mkpath(targetDir);

        // Recursively copy all files and subdirectories
        copyRecursively(sourceDir, targetDir);

        // Use registry to update user PATH
        QSettings settings("HKEY_CURRENT_USER\\Environment", QSettings::NativeFormat);
        QString path = settings.value("PATH").toString();

        if (!path.contains(targetDir, Qt::CaseInsensitive)) {
            if (!path.isEmpty() && !path.endsWith(";")) {
                path.append(";");
            }
            path.append(targetDir);

            settings.setValue("PATH", path);
        }

        qDebug() << "OPM installed successfully to" << targetDir << "and added to path";
        qDebug() << "You may need to restart your terminal for the changes to take effect.";
    }

    bool copyRecursively(const QString &sourcePath, const QString &destinationPath) {
        QDir sourceDir(sourcePath);
        if (!sourceDir.exists()) {
            return false;
        }

        QDir destDir(destinationPath);
        if (!destDir.exists()) {
            destDir.mkpath(".");
        }

        foreach (QString entry, sourceDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Files)) {
            QString sourceEntry = sourcePath + "/" + entry;
            QString destEntry = destinationPath + "/" + entry;

            QFileInfo fileInfo(sourceEntry);
            if (fileInfo.isDir()) {
                // Recursively copy sub-directory
                if (!copyRecursively(sourceEntry, destEntry)) {
                    return false;
                }
            } else {
                // Copy file
                QFile::copy(sourceEntry, destEntry);
            }
        }
        return true;
    }

    void update() {
        fetchManifest();

        if (manifest.isEmpty()) {
            qDebug() << "No available packages found. Please update the manifest.";
            return;
        }

        bool updatesAvailable = false;
        qDebug() << "Checking for updates...";

        for (const auto &package : manifest) {
            QJsonObject pkgObj = package.toObject();
            QString projectName = pkgObj["project_name"].toString();
            QString latestVersion = pkgObj["version"].toString();

            QString installedVersion = installedVersions.value(projectName, "");
            if (!installedVersion.isEmpty() && installedVersion != latestVersion) {
                updatesAvailable = true;
                qDebug() << "Update available for package:" << projectName
                         << "Installed version:" << installedVersion
                         << "Latest version:" << latestVersion;
            }
        }

        if (!updatesAvailable) {
            qDebug() << "All installed packages are up to date.";
        }
    }

    void list() {
        if (manifest.isEmpty()) {
            qDebug() << "No available packages found. Please update the manifest.";
            return;
        }

        qDebug() << "Listing all available packages:";
        qDebug() << "Package Name         Version";
        qDebug() << "-------------------- ----------";

        for (const auto &package : manifest) {
            QJsonObject pkgObj = package.toObject();
            QString projectName = pkgObj["project_name"].toString();
            QString version = pkgObj["version"].toString();

            QString installedVersion = installedVersions.value(projectName, "");

            if (installedVersion.isEmpty()) {
                qDebug().noquote() << QString("%1 %2").arg(projectName.leftJustified(20)).arg(version);
            } else {
                qDebug().noquote() << QString("%1 %2 (Installed: %3)")
                .arg(projectName.leftJustified(20))
                    .arg(version)
                    .arg(installedVersion);
            }
        }
    }

    void install(const QStringList &packageNames) {
        for (const QString &packageName : packageNames) {
            bool found = false;
            for (const auto &package : manifest) {
                QJsonObject pkgObj = package.toObject();
                QString projectName = pkgObj["project_name"].toString();
                QString latestVersion = pkgObj["version"].toString();

                if (QString::compare(projectName, packageName, Qt::CaseInsensitive) == 0) {
                    QString installedVersion = installedVersions.value(projectName, "");

                    if (installedVersion == latestVersion) {
                        qDebug() << projectName << "already installed and up to date.";
                    } else {
                        // Check if the executable is running
                        QProcess process;
                        process.start("tasklist");
                        process.waitForFinished();

                        QString output = process.readAllStandardOutput();
                        bool isRunning = output.contains(projectName + ".exe", Qt::CaseInsensitive);

                        // If the process is running, terminate it
                        if (isRunning) {
                            qDebug() << "Terminating running process:" << projectName;
                            process.start("taskkill", QStringList() << "/F" << "/IM" << (projectName + ".exe"));
                            process.waitForFinished();
                        }

                        QString url = pkgObj["url"].toString();
                        downloadPackage(url, projectName, latestVersion);
                        createStartMenuEntry(projectName);
                        qDebug() << "\nInstalled package:" << projectName;

                        if (isRunning) {
                            qDebug() << "Restarting process:" << projectName;

                            QString processName = QDir::homePath() + "/AppData/Local/Programs/" + projectName + "/" + projectName + ".exe";
                            qDebug() << processName;
                            QProcess::startDetached(processName);

                        }
                    }
                    found = true;
                    break;
                }
            }
            if (!found) {
                qDebug() << "Package not found:" << packageName;
            }
        }
    }

    void remove(const QStringList &packageNames) {
        for (const QString &packageName : packageNames) {
            bool found = false;
            for (const QString &installedPackage : installedVersions.keys()) {
                if (QString::compare(installedPackage, packageName, Qt::CaseInsensitive) == 0) {
                    QString packageDir = QDir::homePath() + "/AppData/Local/Programs/" + installedPackage;
                    if (QDir(packageDir).exists()) {

                        // Check if the executable is running
                        QProcess process;
                        process.start("tasklist");
                        process.waitForFinished();

                        QString output = process.readAllStandardOutput();
                        bool isRunning = output.contains(packageName + ".exe", Qt::CaseInsensitive);

                        // If the process is running, terminate it
                        if (isRunning) {
                            qDebug() << "Terminating running process:" << packageName;
                            process.start("taskkill", QStringList() << "/F" << "/IM" << (packageName + ".exe"));
                            process.waitForFinished();
                        }

                        QDir(packageDir).removeRecursively();
                        installedVersions.remove(installedPackage);
                        saveInstalledPackages();
                        qDebug() << "Removed package:" << installedPackage;

                        QString shortcutPath = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation) + "/" + installedPackage + ".lnk";
                        QFile::remove(shortcutPath);
                    } else {
                        qDebug() << "Package not installed:" << installedPackage;
                    }
                    found = true;
                    break;
                }
            }
            if (!found) {
                qDebug() << "Package not installed:" << packageName;
            }
        }
    }

    void upgrade() {
        bool upgradesFound = false;

        for (const auto &package : manifest) {
            QJsonObject pkgObj = package.toObject();
            QString projectName = pkgObj["project_name"].toString();
            QString version = pkgObj["version"].toString();

            QString installedVersion = installedVersions.value(projectName, "");
            if (installedVersion != version && !installedVersion.isEmpty()) {
                qDebug() << "Upgrading package:" << projectName << "from version" << installedVersion << "to" << version;

                install(QStringList() << projectName);
                upgradesFound = true;
            }
        }

        if (!upgradesFound) {
            qDebug() << "All installed packages are up to date. Nothing to upgrade.";
        }
    }

private:
    QString manifestUrl = "https://raw.githubusercontent.com/Odizinne/opm-manifest/refs/heads/main/manifest.json";
    QString appDataDir;
    QString manifestFile;
    QString installedPackagesFile;
    QJsonArray manifest;
    QMap<QString, QString> installedVersions;

    void fetchManifest() {
        qDebug() << "Fetching manifest from:" << manifestUrl;
        QNetworkAccessManager manager;
        QNetworkReply *reply = manager.get(QNetworkRequest(QUrl(manifestUrl)));

        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();

        if (reply->error() == QNetworkReply::NoError) {
            QByteArray responseData = reply->readAll();
            QJsonDocument jsonDoc = QJsonDocument::fromJson(responseData);
            manifest = jsonDoc.array();

            if (QFile::exists(manifestFile)) {
                QFile::remove(manifestFile);
            }

            QFile manifestFileHandle(manifestFile);
            if (manifestFileHandle.open(QIODevice::WriteOnly)) {
                manifestFileHandle.write(responseData);
                manifestFileHandle.close();
            } else {
                qDebug() << "Error saving manifest file:" << manifestFile;
            }
        } else {
            qDebug() << "Error fetching manifest:" << reply->errorString();
        }
        reply->deleteLater();
    }

    void loadManifest() {
        QFile file(manifestFile);
        if (file.exists() && file.open(QIODevice::ReadOnly)) {
            QByteArray data = file.readAll();
            QJsonDocument jsonDoc = QJsonDocument::fromJson(data);
            manifest = jsonDoc.array();
        }
    }

    void parseInstalledPackages() {
        QFile file(installedPackagesFile);
        if (file.exists() && file.open(QIODevice::ReadOnly)) {
            QByteArray data = file.readAll();
            QJsonDocument jsonDoc = QJsonDocument::fromJson(data);
            QJsonObject jsonObj = jsonDoc.object();
            for (auto it = jsonObj.begin(); it != jsonObj.end(); ++it) {
                installedVersions.insert(it.key(), it.value().toString());
            }
        }
    }

    void downloadPackage(const QString &url, const QString &packageName, const QString &version) {
        qDebug() << "Downloading package from:" << url;
        QNetworkAccessManager manager;
        QNetworkReply *reply = manager.get(QNetworkRequest(QUrl(url)));

        // Wait for the network reply
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();

        if (reply->error() == QNetworkReply::NoError) {
            QByteArray responseData = reply->readAll();
            QString zipFilePath = appDataDir + "/" + packageName + ".zip";
            QFile zipFile(zipFilePath);
            if (zipFile.open(QIODevice::WriteOnly)) {
                zipFile.write(responseData);
                zipFile.close();

                // Extract the zip file
                extractZip(zipFilePath, QDir::homePath() + "/AppData/Local/Programs/", packageName);

                QString extractedDir = QDir::homePath() + "/AppData/Local/Programs/" + packageName;

                if (QDir(extractedDir).exists()) {
                    installedVersions[packageName] = version;
                    saveInstalledPackages();

                    // Remove the zip file after extraction
                    if (!QFile::remove(zipFilePath)) {
                        qDebug() << "Failed to remove zip file:" << zipFilePath;
                    }
                } else {
                    qDebug() << "Extracted directory does not exist:" << extractedDir;
                }
            } else {
                qDebug() << "Error opening ZIP file for writing:" << zipFilePath;
            }
        } else {
            qDebug() << "Error downloading package:" << reply->errorString();
        }
        reply->deleteLater();
    }

    void extractZip(const QString &zipFilePath, const QString &destDir, const QString &packageName) {
        qDebug() << "Installing to:" << destDir + packageName;

        QString program = "powershell";
        QStringList arguments;

        QString command = QString("Expand-Archive -Path '%1' -DestinationPath '%2' -Force").arg(zipFilePath, destDir);

        arguments << "-Command" << command;

        QProcess process;
        process.start(program, arguments);
        process.waitForFinished();

        if (process.exitStatus() == QProcess::CrashExit) {
            qDebug() << "Error extracting ZIP file: Process crashed.";
        } else if (process.exitCode() != 0) {
            qDebug() << "Error extracting ZIP file:" << process.readAllStandardError();
        }
    }

    void saveInstalledPackages() {
        QFile file(installedPackagesFile);
        if (file.open(QIODevice::WriteOnly)) {
            QJsonObject jsonObj;
            for (const auto &key : installedVersions.keys()) {
                jsonObj[key] = installedVersions[key];
            }
            QJsonDocument jsonDoc(jsonObj);
            file.write(jsonDoc.toJson());
            file.close();
        }
    }

    void createStartMenuEntry(const QString &projectName) {
        // Define the shortcut path in the Start Menu
        QString shortcutPath = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation) + "/" + projectName + ".lnk";
        QString targetPath = QDir::homePath() + "/AppData/Local/Programs/" + projectName + "/" + projectName + ".exe"; // Adjust based on your executable

        // Initialize COM
        CoInitialize(NULL);
        IShellLink *pShellLink = nullptr;
        HRESULT hres = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (LPVOID*)&pShellLink);

        if (SUCCEEDED(hres)) {
            // Set the target and description
            pShellLink->SetPath(targetPath.toStdWString().c_str()); // Convert QString to std::wstring
            pShellLink->SetDescription(projectName.toStdWString().c_str()); // Convert QString to std::wstring

            // Set the working directory, ensure to convert to std::wstring
            QString workingDir = QDir::homePath() + "/AppData/Local/Programs/" + projectName;
            pShellLink->SetWorkingDirectory(workingDir.toStdWString().c_str());

            // Save the shortcut
            IPersistFile *pPersistFile;
            hres = pShellLink->QueryInterface(IID_IPersistFile, (LPVOID*)&pPersistFile);
            if (SUCCEEDED(hres)) {
                // Use the wstring conversion for the shortcut path
                hres = pPersistFile->Save(shortcutPath.toStdWString().c_str(), TRUE);
                pPersistFile->Release();
            }

            pShellLink->Release();
            qDebug() << "Created start menu entry.";
        } else {
            qDebug() << "Failed to create ShellLink instance.";
        }

        // Uninitialize COM
        CoUninitialize();
    }

};


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
        manager.selfinstall();
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
