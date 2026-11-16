#pragma once

// Qt before 6.4 plays through GStreamer and drives every audio sink by the
// system clock. With GStreamer 1.20 the sink then works out a skew of
// thousands of hours after a seek, and the sound is gone until the media is
// loaded again. This turns that clock slaving off for each audio sink made
// from now on. Does nothing where Qt plays through FFmpeg, or where the
// program is built without GStreamer.
namespace GStreamerWorkaround
{
// Before the first media player.
void install();
}
