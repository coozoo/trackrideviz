#ifdef HAVE_GSTREAMER
// before Qt's headers: GLib's use words Qt makes macros of
#include <gst/gst.h>
#endif

#include "gstreamerworkaround.h"

#include "diagnostics.h"

#include <QVersionNumber>
#include <QtGlobal>

#ifdef HAVE_GSTREAMER
namespace
{
// A tracer is GStreamer's own way to hear of every element made, in any
// pipeline, without reaching into Qt's.
struct TrackRideVizSinkTracer
{
    GstTracer parent;
};
struct TrackRideVizSinkTracerClass
{
    GstTracerClass parentClass;
};

G_DEFINE_TYPE(TrackRideVizSinkTracer, track_ride_viz_sink_tracer, GST_TYPE_TRACER)

void elementNew(GObject *, GstClockTime, GstElement *element)
{
    // every audio sink has it: pulsesink, alsasink and the rest
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(element), "slave-method") == nullptr)
        return;
    gst_util_set_object_arg(G_OBJECT(element), "slave-method", "none");
    DiagnosticLog::note("gstreamer", QString("%1: clock slaving off").arg(G_OBJECT_TYPE_NAME(element)));
}

void track_ride_viz_sink_tracer_init(TrackRideVizSinkTracer *self)
{
    gst_tracing_register_hook(GST_TRACER(self), "element-new", G_CALLBACK(elementNew));
}

void track_ride_viz_sink_tracer_class_init(TrackRideVizSinkTracerClass *) {}
}
#endif

void GStreamerWorkaround::install()
{
#ifdef HAVE_GSTREAMER
    const bool oldQt = QVersionNumber::fromString(qVersion()) < QVersionNumber(6, 4);
    if (!oldQt && qgetenv("QT_MEDIA_BACKEND") != "gstreamer")
        return;
    gst_init(nullptr, nullptr);
    // kept for the whole run: the hook holds on to it
    g_object_new(track_ride_viz_sink_tracer_get_type(), nullptr);
    gchar *version = gst_version_string();
    DiagnosticLog::note("gstreamer", QString("%1: audio sinks will not follow the system clock").arg(version));
    g_free(version);
#endif
}
