# Unit tests for the automatic mouse mode's decisions (app/streaming/input/automousemode.*)
#   mkdir -p build/tests-automouse && cd build/tests-automouse && qmake6 ../../tests/automouse && make -s && ./tst_automouse
QT = core testlib
CONFIG += c++17 console testcase
CONFIG -= app_bundle
TARGET = tst_automouse
INCLUDEPATH += ../../app/streaming/input
SOURCES += tst_automouse.cpp ../../app/streaming/input/automousemode.cpp
HEADERS += ../../app/streaming/input/automousemode.h
