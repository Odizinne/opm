#include "packagemanager.h"
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
#include <iostream>
#include <windows.h>
#include <shlobj.h>

PackageManager::PackageManager() {
    appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(appDataDir);

    manifestFile = appDataDir + "/manifest.json";
    installedPackagesFile = appDataDir + "/opm_installed_packages.json";

    parseInstalledPackages();
    loadManifest();
}

void PackageManager::help() {
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

void PackageManager::selfinstall() {
    QString sourceDir = QCoreApplication::applicationDirPath();
    QString targetDir = QDir::homePath() + "/AppData/Local/Programs/opm";

    // Ensure the target directory exists
    QDir().mkpath(targetDir);

    // If the target directory already exists, remove its contents
    if (QDir(targetDir).exists()) {
        qDebug() << "Removing existing installation in" << targetDir;
        QDirIterator dirIt(targetDir, QDirIterator::Subdirectories);
        while (dirIt.hasNext()) {
            dirIt.next();
            QFile::remove(dirIt.filePath());
        }
    }

    // Recursively copy all files and subdirectories
    if (!copyRecursively(sourceDir, targetDir)) {
        qDebug() << "Failed to copy files from" << sourceDir << "to" << targetDir;
        return;
    }

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

void PackageManager::promptForManifestUpdate() {
    QTextStream input(stdin);
    QTextStream output(stdout);

    output << "App manifest not found, would you like to update it? (y/n): ";
    output.flush();

    QString response = input.readLine().trimmed().toLower();

    if (response == "y") {
        update();
    } else if (response == "n") {
        output << "Manifest update skipped.\n";
    } else {
        output << "Invalid input. Please enter 'y' or 'n'.\n";
        promptForManifestUpdate();
    }
}

bool PackageManager::copyRecursively(const QString &sourcePath, const QString &destinationPath) {
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
            // Remove existing file before copying
            QFile::remove(destEntry);
            // Copy file
            if (!QFile::copy(sourceEntry, destEntry)) {
                qDebug() << "Failed to copy" << sourceEntry << "to" << destEntry;
                return false;
            }
        }
    }
    return true;
}

void PackageManager::update() {
    fetchManifest();

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

QString PackageManager::greenText(const QString &text) {
    return QString("\033[32m%1\033[0m").arg(text); // 32m for green, 0m to reset
}

void PackageManager::list() {
    if (manifest.isEmpty()) {
        promptForManifestUpdate();
    }

    qDebug() << "Listing all available packages:\n";
    qDebug() << "Package Name         Version";
    qDebug() << "-------------------- ----------";

    for (const auto &package : manifest) {
        QJsonObject pkgObj = package.toObject();
        QString projectName = pkgObj["project_name"].toString();
        QString version = pkgObj["version"].toString();

        QString installedVersion = installedVersions.value(projectName, "");

        QString coloredProjectName = greenText(projectName.leftJustified(20));

        if (installedVersion.isEmpty()) {
            qDebug().noquote() << QString("%1 %2\n").arg(coloredProjectName).arg(version);
        } else {
            qDebug().noquote() << QString("%1 %2 (Installed: %3)\n")
            .arg(coloredProjectName)
                .arg(version)
                .arg(installedVersion);
        }
    }
}

bool PackageManager::checkAndKillProcess(const QString &executable) {
    QProcess process;
    process.start("tasklist");
    process.waitForFinished();

    QString output = process.readAllStandardOutput();
    bool isRunning = output.contains(executable + ".exe", Qt::CaseInsensitive);

    if (isRunning) {
        qDebug() << "Terminating running process:" << executable;
        process.start("taskkill", QStringList() << "/F" << "/IM" << (executable + ".exe"));
        process.waitForFinished();
    }
    return isRunning;
}

void PackageManager::restartProcess(const QString &executable) {
    qDebug() << "Restarting process:" << executable;

    QString processName = QDir::homePath() + "/AppData/Local/Programs/" + executable + "/" + executable + ".exe";
    qDebug() << processName;
    QProcess::startDetached(processName);
}

void PackageManager::install(const QStringList &packageNames) {
    if (manifest.isEmpty()) {
        promptForManifestUpdate();
    }
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

                    bool isRunning = checkAndKillProcess(projectName);

                    QString url = pkgObj["url"].toString();
                    downloadPackage(url, projectName, latestVersion);
                    createStartMenuEntry(projectName);
                    qDebug() << "\nInstalled package:" << projectName;

                    if (isRunning) {
                        restartProcess(projectName);
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

void PackageManager::remove(const QStringList &packageNames) {
    for (const QString &packageName : packageNames) {
        bool found = false;
        for (const QString &installedPackage : installedVersions.keys()) {
            if (QString::compare(installedPackage, packageName, Qt::CaseInsensitive) == 0) {
                QString packageDir = QDir::homePath() + "/AppData/Local/Programs/" + installedPackage;
                if (QDir(packageDir).exists()) {
                    checkAndKillProcess(packageName);

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

void PackageManager::upgrade() {
    if (manifest.isEmpty()) {
        promptForManifestUpdate();
    }

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

void PackageManager::fetchManifest() {
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

void PackageManager::loadManifest() {
    QFile file(manifestFile);
    if (file.exists() && file.open(QIODevice::ReadOnly)) {
        QByteArray data = file.readAll();
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);
        manifest = jsonDoc.array();
    }
}

void PackageManager::parseInstalledPackages() {
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

void PackageManager::downloadPackage(const QString &url, const QString &packageName, const QString &version) {
    qDebug() << "Downloading package...";
    QNetworkAccessManager manager;
    QNetworkReply *reply = manager.get(QNetworkRequest(QUrl(url)));

    QEventLoop loop;

    QObject::connect(reply, &QNetworkReply::downloadProgress, this, [=](qint64 bytesReceived, qint64 bytesTotal) {
        if (bytesTotal > 0) {
            int barWidth = 50;  // Width of the progress bar
            double progress = (double(bytesReceived) / bytesTotal) * 100.0;
            int pos = static_cast<int>(barWidth * bytesReceived / bytesTotal);

            // Build the progress bar
            QString progressBar = QString("[");
            for (int i = 0; i < barWidth; ++i) {
                if (i < pos) progressBar += "=";
                else if (i == pos) progressBar += ">";
                else progressBar += " ";
            }
            progressBar += "] ";

            // Output the progress bar and percentage
            std::cout << "\r" << progressBar.toStdString() << QString::number(progress, 'f', 2).toStdString() << "%";
            std::cout.flush();  // Ensure the output is displayed immediately
        }
    });

    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    loop.exec();

    // Ensure the progress bar reaches 100% when done
    std::cout << std::endl;

    if (reply->error() == QNetworkReply::NoError) {
        QByteArray responseData = reply->readAll();
        QString zipFilePath = appDataDir + "/" + packageName + ".zip";
        QFile zipFile(zipFilePath);
        if (zipFile.open(QIODevice::WriteOnly)) {
            zipFile.write(responseData);
            zipFile.close();

            extractZip(zipFilePath, QDir::homePath() + "/AppData/Local/Programs/", packageName);

            QString extractedDir = QDir::homePath() + "/AppData/Local/Programs/" + packageName;
            if (QDir(extractedDir).exists()) {
                installedVersions[packageName] = version;
                saveInstalledPackages();

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

void PackageManager::extractZip(const QString &zipFilePath, const QString &destDir, const QString &packageName) {
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

void PackageManager::saveInstalledPackages() {
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

void PackageManager::createStartMenuEntry(const QString &projectName) {
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
