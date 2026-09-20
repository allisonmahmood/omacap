QT += core gui qml quick quickcontrols2 multimedia opengl widgets testlib
CONFIG += c++20 console testcase
TARGET = omacap-tests
SOURCES += integration.cpp ../src/model.cpp ../src/theme.cpp ../src/backend.cpp ../src/exporter.cpp
HEADERS += ../src/model.h ../src/theme.h ../src/backend.h ../src/frames.h ../src/exporter.h
RESOURCES += ../resources.qrc
