QT += core gui qml quick quickcontrols2 multimedia opengl widgets
CONFIG += c++20 console
CONFIG -= app_bundle
TARGET = omacap
SOURCES += src/main.cpp src/model.cpp src/theme.cpp src/backend.cpp src/exporter.cpp
HEADERS += src/model.h src/theme.h src/backend.h src/frames.h src/exporter.h
RESOURCES += resources.qrc
QMAKE_CXXFLAGS += -Wall -Wextra
