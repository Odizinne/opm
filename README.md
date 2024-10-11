# OdizinnePackageManager

Spaghetti mess package manager for my projects.

```
+-----------------------------------------------------------------------------------------+
|                                OdizinnePackageManager:                                  |
|-----------------------------------------------------------------------------------------|
|                                                                                         |
|  update                   - Pull latest app manifest and check for available upgrades.  |
|  list                     - List all available packages with their versions.            |
|  install <package_names>  - Install one or more packages.                               |
|  remove <package_names>   - Remove one or more installed packages.                      |
|  upgrade                  - Upgrade installed packages to the latest versions.          |
|  selfinstall              - Install opm in %localappdata%/programs/ and add to path.    |
|  help                     - Display this help message.                                  |
|                                                                                         |
+-----------------------------------------------------------------------------------------+
```


## Install

With powershell:

```
Invoke-Expression (New-Object System.Net.WebClient).DownloadString('https://raw.githubusercontent.com/Odizinne/opm/refs/heads/main/opm_install.ps1')
```
