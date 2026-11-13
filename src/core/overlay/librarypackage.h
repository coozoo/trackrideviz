#pragma once

#include "../gauge/gauge.h"
#include "gaugetemplate.h"

#include <QCoreApplication>

// A gauge or a template as one file, to pass on: .rtvgauge and .rtvtemplate.
//
// Each is gzipped JSON (gzip, zcat and jq read it); plain, not gzipped JSON
// is read as well. A gauge package holds the files of the gauge's folder:
// gauge.json and its drawings (as text) and pictures (base64). A template
// package holds the template and a gauge package for every gauge of the
// user's library it uses; gauges that come with the program are only named.
class LibraryPackage
{
    Q_DECLARE_TR_FUNCTIONS(LibraryPackage)

public:
    static constexpr char GaugeSuffix[] = "rtvgauge";
    static constexpr char TemplateSuffix[] = "rtvtemplate";

    // Empty on failure, *error saying why.
    static QByteArray packGauge(const Gauge &gauge, QString *error = nullptr);
    static bool unpackGauge(const QByteArray &package, Gauge *gauge, QString *error = nullptr);
    // The gauges are taken from the GaugeLibrary by the template's ids.
    static QByteArray packTemplate(const GaugeTemplate &gaugeTemplate, QString *error = nullptr);

    // Into the libraries; the new id, or empty on failure. A gauge the
    // library already holds unchanged is not added twice; a different one of
    // the same name is added under a name of its own.
    static QString importGauge(const QByteArray &package, QString *error = nullptr);
    static QString importTemplate(const QByteArray &package, int *gaugesAdded = nullptr, QString *error = nullptr);
};
