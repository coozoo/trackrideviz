# The core as a static library, built once and linked by every test
# (compiling it into each test took the time of twelve builds).

TEMPLATE = lib
CONFIG += staticlib c++17
TARGET = rtvcore

include(../../src/core/core.pri)
