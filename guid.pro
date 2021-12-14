HEADERS = Guid.h qrcodegen/qrcodegen.hpp
SOURCES = Guid.cpp qrcodegen/qrcodegen.cpp
RESOURCES = guid.qrc
QT += dbus gui widgets
unix:!macx:QT += x11extras
TARGET = guid

unix:!macx:LIBS += -lX11
unix:!macx:DEFINES += WS_X11

win32:CONFIG += console
win64:CONFIG += console

target.path += /usr/bin
INSTALLS += target