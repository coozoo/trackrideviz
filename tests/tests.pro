# Aggregator for the test binaries.
# The core is built once (rtvcore), each test links it.

TEMPLATE = subdirs
CONFIG  += ordered

SUBDIRS = \
    rtvcore \
    channel \
    gpx \
    fit \
    sync \
    gauge \
    settings \
    project \
    export \
    template \
    csv \
    derived \
    laps \
    simple
