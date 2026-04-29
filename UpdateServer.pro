QT       += core gui network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

win32: LIBS += -lversion
VERSION = 1.0.1.3
TARGET = UpdateServer
win32: RC_ICONS += resources/server.ico

DEFINES += APP_VERSION=\\\"$${VERSION}\\\"

DESTDIR = UpdateServer
OBJECTS_DIR = obj_build
MOC_DIR = moc_build
UI_DIR = ui_build
RCC_DIR = rcc_build
SOURCES += \
    main.cpp \
    mainwindow.cpp \
    httpserver.cpp \
    appupdatemanager.cpp \
    appeditdialog.cpp \
    docmanager.cpp \
    doceditdialog.cpp \
    docwindow.cpp \
    logger.cpp \
    usermanager.cpp \
    usereditdialog.cpp \
    ../AppManager/versionutils.cpp

HEADERS += \
    mainwindow.h \
    httpserver.h \
    appupdatemanager.h \
    appeditdialog.h \
    docentry.h \
    docmanager.h \
    doceditdialog.h \
    docwindow.h \
    logger.h \
    usermanager.h \
    usereditdialog.h \
    ../AppManager/versionutils.h

RESOURCES += \
    updateserver.qrc

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
