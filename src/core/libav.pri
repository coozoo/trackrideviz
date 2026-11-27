# The program's own video engine: FFmpeg's libraries (libav) linked in, when they are there
# and new enough - FFmpeg 6.1 or later (libavformat 60.16). Without them the program works
# through the ffmpeg program as before. CONFIG+=no_libav builds without them anyway.
# Included by core.pri, and by whatever links the core as a library (the tests).

RTV_PKG_CONFIG = $$pkgConfigExecutable()
!no_libav:!android:packagesExist(libavformat libavcodec libavfilter libavutil libswresample libswscale) {
    system($$RTV_PKG_CONFIG --atleast-version=60.16.100 libavformat) {
        CONFIG += link_pkgconfig
        PKGCONFIG += libavformat libavcodec libavfilter libavutil libswresample libswscale
        DEFINES += RTV_LIBAV
        !build_pass:message("With FFmpeg's libraries $$system($$RTV_PKG_CONFIG --modversion libavformat): the program's own video engine is built in")
    } else {
        message("FFmpeg's libraries are older than 6.1: built without the program's own video engine")
    }
}

# Android: an FFmpeg built for it (CI builds it, RTV_ANDROID_FFMPEG names where), not the build
# machine's. Its libraries carry the suffix _rtv: Qt's multimedia brings an FFmpeg of its own,
# without the filters, under the plain names.
android:!no_libav {
    RTV_FFMPEG = $$(RTV_ANDROID_FFMPEG)
    !isEmpty(RTV_FFMPEG) {
        RTV_FFMPEG_LIBS = avformat avcodec avfilter avutil swresample swscale
        INCLUDEPATH += $$RTV_FFMPEG/include
        LIBS += -L$$RTV_FFMPEG/lib
        for(name, RTV_FFMPEG_LIBS) {
            LIBS += -l$${name}_rtv
            ANDROID_EXTRA_LIBS += $$RTV_FFMPEG/lib/lib$${name}_rtv.so
        }
        DEFINES += RTV_LIBAV
        !build_pass:message("With FFmpeg's libraries from $$RTV_FFMPEG: the program's own video engine is built in")
    }
}
