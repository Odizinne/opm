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

class PackageManager {
public:
    PackageManager() {
        appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(appDataDir); // Ensure the directory exists

        manifestFile = appDataDir + "/manifest.json";
        installedPackagesFile = appDataDir + "/opm_installed_packages.json";

        // Load installed packages from JSON if exists
        parseInstalledPackages();

        // Load the manifest file
        loadManifest();  // Attempt to load the manifest on startup
    }

    void help() {
        qDebug() << "OdizinnePackageManager:";
        qDebug() << "Available commands:";
        qDebug() << "  update                   - Pull latest app manifest and check for available upgrades.";
        qDebug() << "  list                     - List all available packages with their versions.";
        qDebug() << "  install <package_names>  - Install one or more packages.";
        qDebug() << "  remove <package_names>   - Remove one or more installed packages.";
        qDebug() << "  upgrade                  - Upgrade installed packages to the latest versions.";
        qDebug() << "  help                     - Display this help message.";
    }

    void update() {
        fetchManifest();

        // Check for updates after fetching the latest manifest
        if (manifest.isEmpty()) {
            qDebug() << "No available packages found. Please update the manifest.";
            return;
        }

        bool updatesAvailable = false;
        qDebug() << "Checking for updates...";

        // Iterate over installed packages to check for updates
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

        // Print header without quotes
        qDebug() << "Listing all available packages:";
        qDebug() << "Package Name         Version";  // Header
        qDebug() << "-------------------- ----------"; // Separator line

        for (const auto &package : manifest) {
            QJsonObject pkgObj = package.toObject();
            QString projectName = pkgObj["project_name"].toString();
            QString version = pkgObj["version"].toString();

            // Check if the package is installed
            QString installedVersion = installedVersions.value(projectName, "");

            // Format output without quotes
            if (installedVersion.isEmpty()) {
                // If not installed, just show the package name and version
                qDebug().noquote() << QString("%1 %2").arg(projectName.leftJustified(20)).arg(version);
            } else {
                // If installed, show the package name, version, and the installed version
                qDebug().noquote() << QString("%1 %2 (Installed: %3)")
                                          .arg(projectName.leftJustified(20))
                                          .arg(version)
                                          .arg(installedVersion);
            }
        }
    }

    // Updated install function to accept multiple package names and handle case insensitivity
    void install(const QStringList &packageNames) {
        for (const QString &packageName : packageNames) {
            bool found = false;
            for (const auto &package : manifest) {
                QJsonObject pkgObj = package.toObject();
                QString projectName = pkgObj["project_name"].toString();
                QString latestVersion = pkgObj["version"].toString();

                // Compare case-insensitively
                if (QString::compare(projectName, packageName, Qt::CaseInsensitive) == 0) {
                    QString installedVersion = installedVersions.value(projectName, "");

                    // Check if the package is already installed and up to date
                    if (installedVersion == latestVersion) {
                        qDebug() << projectName << "already installed and up to date.";
                    } else {
                        // Proceed with installation if not up to date
                        QString url = pkgObj["url"].toString();
                        downloadPackage(url, projectName, latestVersion); // Pass the latest version to downloadPackage
                        qDebug() << "Installed package:" << projectName;
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

    // Updated remove function to accept multiple package names and handle case insensitivity
    void remove(const QStringList &packageNames) {
        for (const QString &packageName : packageNames) {
            bool found = false; // Flag to check if package was found
            for (const QString &installedPackage : installedVersions.keys()) {
                // Compare case-insensitively
                if (QString::compare(installedPackage, packageName, Qt::CaseInsensitive) == 0) {
                    QString packageDir = QDir::homePath() + "/AppData/Local/Programs/" + installedPackage;
                    if (QDir(packageDir).exists()) {
                        QDir(packageDir).removeRecursively();
                        installedVersions.remove(installedPackage);
                        saveInstalledPackages();
                        qDebug() << "Removed package:" << installedPackage;
                    } else {
                        qDebug() << "Package not installed:" << installedPackage;
                    }
                    found = true; // Set flag to true if the package was found and processed
                    break;
                }
            }
            if (!found) {
                qDebug() << "Package not installed:" << packageName;
            }
        }
    }

    void upgrade() {
        bool upgradesFound = false; // Flag to track if any upgrades are done

        for (const auto &package : manifest) {
            QJsonObject pkgObj = package.toObject();
            QString projectName = pkgObj["project_name"].toString();
            QString version = pkgObj["version"].toString();

            QString installedVersion = installedVersions.value(projectName, "");
            if (installedVersion != version && !installedVersion.isEmpty()) {
                qDebug() << "Upgrading package:" << projectName << "from version" << installedVersion << "to" << version;
                install(QStringList() << projectName); // Call the updated install method
                upgradesFound = true; // Set flag to true if an upgrade is initiated
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

        // Wait for the network reply
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();

        if (reply->error() == QNetworkReply::NoError) {
            QByteArray responseData = reply->readAll();
            QJsonDocument jsonDoc = QJsonDocument::fromJson(responseData);
            manifest = jsonDoc.array();

            // Check if the manifest file exists and remove it
            if (QFile::exists(manifestFile)) {
                QFile::remove(manifestFile);
            }

            // Save the new manifest to the app data directory
            QFile manifestFileHandle(manifestFile);
            if (manifestFileHandle.open(QIODevice::WriteOnly)) {
                manifestFileHandle.write(responseData);
                manifestFileHandle.close();
                qDebug() << "Manifest saved to:" << manifestFile;
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
            QString zipFilePath = QDir::homePath() + "/Downloads/" + packageName + ".zip";
            QFile zipFile(zipFilePath);
            if (zipFile.open(QIODevice::WriteOnly)) {
                zipFile.write(responseData);
                zipFile.close();
                qDebug() << "Downloaded:" << zipFilePath;

                // Extract the downloaded zip file
                extractZip(zipFilePath, QDir::homePath() + "/AppData/Local/Programs/");

                // After extraction, create the full path to the package's directory
                QString extractedDir = QDir::homePath() + "/AppData/Local/Programs/" + packageName;

                // Check if the package was successfully extracted
                if (QDir(extractedDir).exists()) {
                    installedVersions[packageName] = version; // Update installed version with actual version from manifest
                    saveInstalledPackages();
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

    void extractZip(const QString &zipFilePath, const QString &destDir) {
        qDebug() << "Extracting ZIP file to:" << destDir;

        // Prepare the PowerShell command to extract the ZIP file
        QString program = "powershell";
        QStringList arguments;

        // Create the PowerShell command string to unzip the file
        QString command = QString("Expand-Archive -Path '%1' -DestinationPath '%2' -Force").arg(zipFilePath, destDir);

        // Add the command to the arguments
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
        QJsonObject jsonObj;
        for (const auto &key : installedVersions.keys()) {
            jsonObj.insert(key, installedVersions.value(key)); // Save the actual installed version
        }
        QJsonDocument jsonDoc(jsonObj);
        QFile file(installedPackagesFile);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(jsonDoc.toJson());
            file.close();
        } else {
            qDebug() << "Error saving installed packages file:" << installedPackagesFile;
        }
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

    if (command == "update") {
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
