#pragma once

#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QMap>
#include <QStringList>

#include <algorithm>

// How long something done again and again takes (a picture painted, a tile downloaded), said
// every couple of seconds as "rtv.perf: <what>: N times, average and longest ms", and kept for
// the simplified interface to show (summary()). On Android only; elsewhere the category is off.
// Used from the program's main thread.
inline const QLoggingCategory &perfLog()
{
#ifdef Q_OS_ANDROID
    static const QLoggingCategory category("rtv.perf", QtInfoMsg);
#else
    static const QLoggingCategory category("rtv.perf", QtWarningMsg);
#endif
    return category;
}

class PerfMeter
{
public:
    explicit PerfMeter(const char *what) : m_what(what) {}

    // One more time it took `ms`; `failed`: it did not work out.
    void add(double ms, bool failed = false)
    {
        if (!perfLog().isInfoEnabled())
            {
                return;
            }
        if (!m_window.isValid())
            {
                m_window.start();
            }
        ++m_count;
        m_failed += failed ? 1 : 0;
        m_total += ms;
        m_longest = std::max(m_longest, ms);
        if (m_window.elapsed() >= 2000)
            {
                const QString line = QString("%1: %2 in 2 s, average %3 ms, longest %4 ms%5")
                                     .arg(QLatin1String(m_what)).arg(m_count).arg(qRound(m_total / m_count))
                                     .arg(qRound(m_longest))
                                     .arg(m_failed > 0 ? QString(", %1 failed").arg(m_failed) : QString());
                qCInfo(perfLog()).noquote() << line;
                lines()[QLatin1String(m_what)] = line;
                m_count = 0;
                m_failed = 0;
                m_total = 0.0;
                m_longest = 0.0;
                m_window.restart();
            }
    }

    // The last word of each meter, a line each.
    static QString summary() { return QStringList(lines().values()).join('\n'); }

    // Timed from here to the end of the block.
    class Scope
    {
    public:
        explicit Scope(PerfMeter &meter) : m_meter(meter) { m_took.start(); }
        ~Scope() { m_meter.add(m_took.nsecsElapsed() / 1e6); }
        Q_DISABLE_COPY_MOVE(Scope)

    private:
        PerfMeter &m_meter;
        QElapsedTimer m_took;
    };

private:
    static QMap<QString, QString> &lines()
    {
        static QMap<QString, QString> all;
        return all;
    }

    const char *m_what;
    QElapsedTimer m_window;
    int m_count{0};
    int m_failed{0};
    double m_total{0.0};
    double m_longest{0.0};
};
