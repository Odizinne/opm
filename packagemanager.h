#ifndef PACKAGEMANAGER_H
#define PACKAGEMANAGER_H

#include <QString>
#include <QJsonArray>

class PackageManager : public QObject
{
    Q_OBJECT

public:
    PackageManager();
    void help();
    void selfinstall();
    void update();
    void list();
    bool copyRecursively(const QString &sourcePath, const QString &destinationPath);
    QString greenText(const QString &text);
    bool checkAndKillProcess(const QString &executable);
    void restartProcess(const QString &executable);
    void install(const QStringList &packageNames);
    void remove(const QStringList &packageNames);
    void upgrade();

private:
    QString manifestUrl = "https://raw.githubusercontent.com/Odizinne/opm-manifest/refs/heads/main/manifest.json";
    QString appDataDir;
    QString manifestFile;
    QString installedPackagesFile;
    QJsonArray manifest;
    QMap<QString, QString> installedVersions;
    void fetchManifest();
    void loadManifest();
    void parseInstalledPackages();
    void downloadPackage(const QString &url, const QString &packageName, const QString &version);
    void extractZip(const QString &zipFilePath, const QString &destDir, const QString &packageName);
    void saveInstalledPackages();
    void createStartMenuEntry(const QString &projectName);


};

#endif // PACKAGEMANAGER_H
