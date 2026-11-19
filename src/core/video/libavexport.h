#pragma once

#include "exportplan.h"

#include <QImage>
#include <QString>

#include <functional>
#include <memory>

// Runs an ExportPlan in-process with FFmpeg's libraries (see Libav), as the ffmpeg program
// would run its command line: the same inputs, the same filter graph, the same encoders
// and options. The gauges are handed over as pictures rather than through a pipe.
//
// Not a QObject and not threaded: ExportJob calls step() from its worker thread again and
// again, each call doing a little, so a stop is seen between them.
class LibavExport
{
public:
    // The picture of the gauges for output frame `index`: the stacked regions of the plan,
    // in QImage::Format_ARGB32_Premultiplied (Format_ARGB32 for a transparent plan).
    using DrawGauges = std::function<QImage(qint64 index)>;

    LibavExport(const ExportPlan &plan, DrawGauges draw);
    ~LibavExport();
    LibavExport(const LibavExport &) = delete;
    LibavExport &operator=(const LibavExport &) = delete;

    // Opens the inputs, builds the graph, opens the encoders and the output file.
    bool start(QString *error);

    enum State
    {
        Working,
        Done,       // the file is complete
        Failed,     // see error(); the file is not worth keeping
    };
    // Does a slice of the work.
    State step();
    // No more pictures after those sent: the file ends there, its sound cut to the same
    // length (what closing ffmpeg's pipe does).
    void endGauges();

    qint64 gaugesSent() const;
    QString error() const;

    // Built with the libraries (as Libav::available()).
    static bool available();

private:
    struct Private;
    std::unique_ptr<Private> d;
};
