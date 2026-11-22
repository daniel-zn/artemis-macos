# Unit tests for Artemis for Mac's self-update decisions and checks (app/backend/macupdate.*)
#   mkdir -p build/tests-macupdate && cd build/tests-macupdate && qmake6 ../../tests/macupdate && make -s && ./tst_macupdate
QT = core testlib
CONFIG += c++17 console testcase
CONFIG -= app_bundle
TARGET = tst_macupdate
INCLUDEPATH += ../../app/backend
SOURCES += tst_macupdate.cpp
OBJECTIVE_SOURCES += ../../app/backend/macupdate.mm
HEADERS += ../../app/backend/macupdate.h
LIBS += -framework Foundation -framework Security
