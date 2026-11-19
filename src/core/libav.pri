# The program's own video engine: FFmpeg's libraries (libav) linked in, when they are there
# and new enough - FFmpeg 6.1 or later (libavformat 60.16). Without them the program works
# through the ffmpeg program as before. CONFIG+=no_libav builds without them anyway.
# Included by core.pri, and by whatever links the core as a library (the tests).

RTV_PKG_CONFIG = $$pkgConfigExecutable()
!no_libav:packagesExist(libavformat libavcodec libavfilter libavutil libswresample) {
    system($$RTV_PKG_CONFIG --atleast-version=60.16.100 libavformat) {
        CONFIG += link_pkgconfig
        PKGCONFIG += libavformat libavcodec libavfilter libavutil libswresample
        DEFINES += RTV_LIBAV
        !build_pass:message("With FFmpeg's libraries $$system($$RTV_PKG_CONFIG --modversion libavformat): the program's own video engine is built in")
    } else {
        message("FFmpeg's libraries are older than 6.1: built without the program's own video engine")
    }
}
