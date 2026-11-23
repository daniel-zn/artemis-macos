# Unit tests for the Mac keyboard shortcuts on the host (app/streaming/input/macshortcuts.*)
#   mkdir -p build/tests-macshortcuts && cd build/tests-macshortcuts && qmake6 ../../tests/macshortcuts && make -s && ./tst_macshortcuts
QT = core testlib
CONFIG += c++17 console testcase
CONFIG -= app_bundle
TARGET = tst_macshortcuts
INCLUDEPATH += ../../app/streaming/input
SOURCES += tst_macshortcuts.cpp ../../app/streaming/input/macshortcuts.cpp
HEADERS += ../../app/streaming/input/macshortcuts.h
