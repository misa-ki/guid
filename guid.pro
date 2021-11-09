HEADERS = Guid.h
SOURCES = Guid.cpp
QT += dbus gui widgets
unix:!macx:QT += x11extras
TARGET = guid

unix:!macx:LIBS += -lX11
unix:!macx:DEFINES += WS_X11

target.path += /usr/bin
INSTALLS += target
