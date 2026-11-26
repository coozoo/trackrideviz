#pragma once

#include <QDateTime>
#include <QMutex>
#include <QMutexLocker>
#include <QString>
#include <QStringList>

// The last things the program said (Qt's messages and its own), kept in memory for the
// simplified interface to copy: on a phone, where reading logcat takes a computer or a log viewer.
// Safe from any thread.
namespace AppLog
{
inline QMutex &mutex()
{
    static QMutex lock;
    return lock;
}

inline QStringList &lines()
{
    static QStringList kept;
    return kept;
}

inline void add(const QString &line)
{
    constexpr int kKept = 3000;
    const QMutexLocker lock(&mutex());
    lines().append(QTime::currentTime().toString("HH:mm:ss.zzz ") + line);
    if (lines().size() > kKept)
        {
            lines().erase(lines().begin(), lines().begin() + (lines().size() - kKept));
        }
}

inline QString text()
{
    const QMutexLocker lock(&mutex());
    return lines().join('\n');
}
}
