# Unit tests for the shared clipboard's decisions (app/streaming/clipboardsyncstate.*)
#   mkdir -p build/tests && cd build/tests && qmake6 ../../tests/clipboardsync && make -s && ./tst_clipboardsync
QT = core testlib
CONFIG += c++17 console testcase
CONFIG -= app_bundle
TARGET = tst_clipboardsync
INCLUDEPATH += ../../app/streaming
SOURCES += tst_clipboardsync.cpp ../../app/streaming/clipboardsyncstate.cpp
HEADERS += ../../app/streaming/clipboardsyncstate.h
