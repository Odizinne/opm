QT = core network

CONFIG += c++17 cmdline

DEPENDENCIES_DIR = $$PWD/Dependencies
DEST_DIR = $$OUT_PWD/release/Dependencies

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
        main.cpp \
        packagemanager.cpp

LIBS += -lShell32 -lole32

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

HEADERS += \
    packagemanager.h

QMAKE_POST_LINK += powershell -Command "New-Item -ItemType Directory -Path '$$DEST_DIR' -Force; Copy-Item -Path '$$DEPENDENCIES_DIR\*' -Destination '$$DEST_DIR' -Recurse -Force"
