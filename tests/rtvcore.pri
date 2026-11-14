# Included by every test .pro: the core's headers and the library built by
# rtvcore/rtvcore.pro (first in tests.pro).

QT += svg network
INCLUDEPATH += $$PWD/../src/core

LIBS += -L$$OUT_PWD/../rtvcore -lrtvcore -lz
PRE_TARGETDEPS += $$OUT_PWD/../rtvcore/librtvcore.a

# Qt's own headers trip this warning of GCC 16; nothing of ours to fix
*-g++*: QMAKE_CXXFLAGS_WARN_ON += -Wno-sfinae-incomplete
