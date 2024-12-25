import sys
from PackageManager import PackageManager


def main():
    if len(sys.argv) < 2:
        print("Usage: opm <command> [package_name ...]")
        return 1

    command = sys.argv[1]
    packageNames = sys.argv[2:]

    manager = PackageManager()

    if command == "selfinstall":
        manager.self_install()
    elif command == "selfupdate":
        manager.self_update()
    elif command == "update":
        manager.update()
    elif command == "list":
        manager.list()
    elif command == "install":
        manager.install(packageNames)
    elif command == "remove":
        manager.remove(packageNames)
    elif command == "upgrade":
        manager.upgrade()
    elif command == "help":
        manager.help()
    else:
        print("Unknown command or missing arguments.\nTry running opm help")


if __name__ == "__main__":
    main()
