import os
import sys
import json
import shutil
import requests
from pathlib import Path
from hashlib import sha256
from winreg import OpenKey, SetValueEx, HKEY_CURRENT_USER, KEY_WRITE
from win32com.client import Dispatch
from subprocess import Popen
import zipfile
import psutil
from colorama import init, Fore


class PackageManager:
    def __init__(self):
        init(autoreset=True)  # Initialize colorama
        self.app_data_dir = str(Path(os.getenv("APPDATA")).joinpath("opm"))
        os.makedirs(self.app_data_dir, exist_ok=True)

        self.manifest_file = os.path.join(self.app_data_dir, "manifest.json")
        self.installed_packages_file = os.path.join(self.app_data_dir, "opm_installed_packages.json")

        self.installed_versions = {}
        self.manifest = []

        self.load_installed_packages()
        self.load_manifest()

    def help(self):
        print("+-----------------------------------------------------------------------------------------+")
        print("|                                OdizinnePackageManager:                                  |")
        print("|-----------------------------------------------------------------------------------------|")
        print("|                                                                                         |")
        print("|  update                   - Pull latest app manifest and check for available upgrades.  |")
        print("|  list                     - List all available packages with their versions.            |")
        print("|  install <package_names>  - Install one or more packages.                               |")
        print("|  remove <package_names>   - Remove one or more installed packages.                      |")
        print("|  upgrade                  - Upgrade installed packages to the latest versions.          |")
        print("|  selfinstall              - Install opm in %localappdata%/programs/ and add to path.    |")
        print("|  help                     - Display this help message.                                  |")
        print("|                                                                                         |")
        print("+-----------------------------------------------------------------------------------------+")

    def self_install(self):
        source_dir = os.getcwd()
        target_dir = os.path.join(os.getenv("LOCALAPPDATA"), "Programs", "opm")

        if os.path.exists(target_dir):
            print("Removing existing installation...")
            shutil.rmtree(target_dir)

        print(f"Copying files from {source_dir} to {target_dir}")
        shutil.copytree(source_dir, target_dir)

        # Add to PATH in registry
        key = OpenKey(HKEY_CURRENT_USER, r"Environment", 0, KEY_WRITE)
        path = self._get_registry_value(key, "PATH")

        if target_dir not in path:
            if path:
                path += ";"
            path += target_dir
            SetValueEx(key, "PATH", 0, 1, path)

        print(f"OPM installed successfully to {target_dir} and added to path.")
        print("You may need to restart your terminal for the changes to take effect.")

    def _get_registry_value(self, key, value_name):
        try:
            value, _ = key.QueryValueEx(value_name)
            return value
        except FileNotFoundError:
            return ""

    def prompt_for_manifest_update(self):
        response = input("App manifest not found, would you like to update it? (y/n): ").strip().lower()
        if response == "y":
            self.update()
        elif response == "n":
            print("Manifest update skipped.")
        else:
            print("Invalid input. Please enter 'y' or 'n'.")
            self.prompt_for_manifest_update()

    def update(self):
        current_manifest_file_path = self.manifest_file
        previous_manifest_hash = self.compute_manifest_hash(current_manifest_file_path)

        self.fetch_manifest()

        new_manifest_hash = self.compute_manifest_hash(current_manifest_file_path)

        if previous_manifest_hash != new_manifest_hash:
            print("Manifest updated.")
        else:
            print("Manifest is already up to date.")

        updates_available = False
        for package in self.manifest:
            project_name = package["project_name"]
            latest_version = package["version"]

            # Ensure case-insensitive matching of the project name
            installed_version = None
            for installed_package in self.installed_versions:
                if installed_package.lower() == project_name.lower():
                    installed_version = self.installed_versions[installed_package]
                    break

            if installed_version and installed_version != latest_version:
                updates_available = True
                print(
                    f"\nUpdate available for package: {project_name} - Installed: {installed_version}, Latest: {latest_version}"
                )

        if not updates_available:
            print("All installed packages are up to date.")

        self.check_opm_update()

    def self_update(self):
        print("Running self-update... Please wait.")
        url = "https://raw.githubusercontent.com/Odizinne/opm/refs/heads/main/opm_install.ps1"
        Popen(
            [
                "powershell.exe",
                "-Command",
                f"Invoke-Expression (New-Object System.Net.WebClient).DownloadString('{url}')",
            ]
        )

    def check_opm_update(self):
        print("\nChecking for OPM updates...")
        version_file_path = os.path.join(self.app_data_dir, "version")
        if not os.path.exists(version_file_path):
            return

        with open(version_file_path, "r") as file:
            local_version = int(file.read().strip())

        response = requests.get("https://api.github.com/repos/odizinne/opm/releases/latest")
        if response.status_code == 200:
            latest_version = response.json()["tag_name"]
            remote_version = int(latest_version.lstrip("v"))

            if remote_version > local_version:
                print(f"OPM v{remote_version} is available. Run 'opm selfupdate' to install the latest version.")
            else:
                print("OPM is up to date.")
        else:
            print("Failed to check for OPM updates.")

    def compute_manifest_hash(self, file_path):
        try:
            with open(file_path, "rb") as file:
                data = file.read()
                return sha256(data).hexdigest()
        except FileNotFoundError:
            print(f"Error: Manifest file not found: {file_path}")
            return ""

    def list(self):
        if not self.manifest:
            self.prompt_for_manifest_update()

        print("Listing all available packages:")
        print("")

        for package in self.manifest:
            project_name = package["project_name"]
            version = package["version"]
            description = package["description"]
            installed_version = self.installed_versions.get(project_name, "")

            # Color project name in green
            project_name_colored = f"{Fore.GREEN}{project_name}{Fore.RESET}"

            if installed_version:
                print(f"{project_name_colored:<20} {version} (Installed: {installed_version})")
            else:
                print(f"{project_name_colored:<20} {version}")

            print(f"{description}\n")

    def install(self, package_names):
        if not self.manifest:
            self.prompt_for_manifest_update()

        for package_name in package_names:
            found = False
            for package in self.manifest:
                if package["project_name"].lower() == package_name.lower():
                    installed_version = self.installed_versions.get(package_name, "")
                    if installed_version == package["version"]:
                        print(f"{package["project_name"]} is already installed and up to date.")
                    else:
                        print(f"Installing {package["project_name"]} {package["version"]}...")
                        print("")
                        self.download_package(package["url"], package["project_name"], package["version"])
                    found = True
                    break
            if not found:
                print(f"Package {package_name} not found.")

    def download_package(self, url, package_name, version):
        response = requests.get(url, stream=True)
        zip_file_path = os.path.join(self.app_data_dir, f"{package_name}.zip")
        total_size = int(response.headers.get("Content-Length", 0))

        with open(zip_file_path, "wb") as file:
            downloaded_size = 0
            for chunk in response.iter_content(chunk_size=1024):
                if chunk:
                    file.write(chunk)
                    downloaded_size += len(chunk)
                    self.show_progress_bar(downloaded_size, total_size, "Downloading:")

        print()  # Move to the next line after the progress bar
        self.extract_zip(zip_file_path, package_name)
        self.installed_versions[package_name] = version
        self.save_installed_packages()
        self.create_start_menu_shortcut(package_name)

    def show_progress_bar(self, downloaded, total, action=""):
        progress = downloaded / total
        bar_length = 40
        block = int(round(bar_length * progress))

        # Color based on progress
        if progress < 0.33:
            color = Fore.RED  # Red for early stages
        elif progress < 0.66:
            color = Fore.YELLOW  # Yellow for mid stages
        else:
            color = Fore.GREEN  # Green for nearing completion

        # Creating the progress bar string
        progress_str = (
            f"\r{action} [{color}{'#' * block}{'-' * (bar_length - block)}{Fore.RESET}] {round(progress * 100, 2)}%"
        )
        sys.stdout.write(progress_str)
        sys.stdout.flush()

    def extract_zip(self, zip_file_path, package_name):
        dest_dir = os.path.join(os.getenv("LOCALAPPDATA"), "Programs")
        if not os.path.exists(dest_dir):
            os.makedirs(dest_dir)

        with zipfile.ZipFile(zip_file_path, "r") as zip_ref:
            files = zip_ref.namelist()
            total_files = len(files)
            extracted = 0

            for file in files:
                zip_ref.extract(file, dest_dir)
                extracted += 1
                self.show_progress_bar(extracted, total_files, "Extracting: ")

        os.remove(zip_file_path)
        print()  # Move to the next line after the extraction progress bar

    def remove(self, package_names):
        for package_name in package_names:
            package_name_lower = package_name.lower()
            found = False

            for installed_package in self.installed_versions.keys():
                if installed_package.lower() == package_name_lower:
                    found = True
                    break

            if found:
                self.kill_package_process(package_name)

                installed_path = os.path.join(os.getenv("LOCALAPPDATA"), "Programs", package_name)
                if os.path.exists(installed_path):
                    shutil.rmtree(installed_path)
                    del self.installed_versions[installed_package]
                    self.save_installed_packages()

                    self.delete_start_menu_shortcut(package_name)
                    print(f"Removed {package_name}")

                else:
                    print(f"Package {package_name} is not installed.")
            else:
                print(f"Package {package_name} is not installed.")

    def kill_package_process(self, package_name):
        for proc in psutil.process_iter(["pid", "name"]):
            if package_name.lower() in proc.info["name"].lower():
                try:
                    print(f"Terminating process {proc.info['name']} (PID: {proc.info['pid']})...")
                    proc.terminate()
                    proc.wait()
                except (psutil.NoSuchProcess, psutil.AccessDenied, psutil.ZombieProcess):
                    continue

    def upgrade(self):
        if not self.manifest:
            self.prompt_for_manifest_update()

        updates_available = False
        for package in self.manifest:
            project_name = package["project_name"]
            latest_version = package["version"]

            # Case-insensitive search for the installed version
            installed_version = None
            for installed_package in self.installed_versions:
                if installed_package.lower() == project_name.lower():
                    installed_version = self.installed_versions[installed_package]
                    break

            if installed_version and installed_version != latest_version:
                updates_available = True
                print(f"\nUpgrading {project_name} from version {installed_version} to {latest_version}")
                self.install([project_name])

        if not updates_available:
            print("All installed packages are up to date.")

    def fetch_manifest(self):
        print("Fetching manifest from the server...")
        response = requests.get("https://raw.githubusercontent.com/Odizinne/opm-manifest/main/manifest.json")
        if response.status_code == 200:
            self.manifest = response.json()
            with open(self.manifest_file, "w") as file:
                json.dump(self.manifest, file, indent=4)
        else:
            print("Failed to fetch manifest.")

    def load_manifest(self):
        if os.path.exists(self.manifest_file):
            with open(self.manifest_file, "r") as file:
                self.manifest = json.load(file)

    def load_installed_packages(self):
        if os.path.exists(self.installed_packages_file):
            with open(self.installed_packages_file, "r") as file:
                self.installed_versions = json.load(file)

    def save_installed_packages(self):
        with open(self.installed_packages_file, "w") as file:
            json.dump(self.installed_versions, file, indent=4)

    def create_start_menu_shortcut(self, package_name):
        installed_dir = os.path.join(os.getenv("LOCALAPPDATA"), "Programs", package_name)
        executable_name = None

        for file in os.listdir(installed_dir):
            if file.lower().endswith(".exe"):
                executable_name = file
                break

        if not executable_name:
            print(f"No executable file found for {package_name}, skipping shortcut creation.")
            return

        shortcut_name = os.path.splitext(executable_name)[0]
        package_path = os.path.join(installed_dir, executable_name)
        start_menu_folder = os.path.join(os.getenv("APPDATA"), "Microsoft", "Windows", "Start Menu", "Programs")
        shortcut_path = os.path.join(start_menu_folder, f"{shortcut_name}.lnk")

        shell = Dispatch("WScript.Shell")
        shortcut = shell.CreateShortCut(shortcut_path)
        shortcut.TargetPath = package_path
        shortcut.WorkingDirectory = os.path.dirname(package_path)
        shortcut.IconLocation = package_path
        shortcut.save()

        print(f"Created Start Menu shortcut for {shortcut_name}.")

    def delete_start_menu_shortcut(self, package_name):
        shortcut_name = package_name
        start_menu_folder = os.path.join(os.getenv("APPDATA"), "Microsoft", "Windows", "Start Menu", "Programs")
        shortcut_path = os.path.join(start_menu_folder, f"{shortcut_name}.lnk")

        if os.path.exists(shortcut_path):
            os.remove(shortcut_path)
