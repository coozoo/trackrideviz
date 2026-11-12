#pragma once

#include "gauge.h"

#include <QCoreApplication>

// The native gauge format: a folder holding `gauge.json` and the images it
// refers to. Self-contained, so a gauge can be copied, zipped and shared.
class GaugeFile
{
    Q_DECLARE_TR_FUNCTIONS(GaugeFile)

public:
    static constexpr char FileName[] = "gauge.json";

    // Reads `folder`/gauge.json and loads its images.
    static bool load(const QString &folder, Gauge *gauge, QString *error = nullptr);
    // Writes gauge.json and every image into `folder` (created if missing).
    // Images go where their imagePath says; those without a path get one.
    static bool save(const QString &folder, const Gauge &gauge, QString *error = nullptr);

    // The description alone. Images are neither written nor loaded.
    static QByteArray toJson(const Gauge &gauge);
    static bool fromJson(const QByteArray &json, Gauge *gauge, QString *error = nullptr);
};
