#include "playback.h"

#include <algorithm>

DataClock::DataClock(QObject *parent)
    : QObject(parent)
{
    m_ticker.setInterval(33);
    connect(&m_ticker, &QTimer::timeout, this, &DataClock::tick);
}

qint64 DataClock::position() const
{
    if (m_state != QMediaPlayer::PlayingState)
        {
            return m_start;
        }
    return std::min(m_duration, m_start + m_playing.elapsed());
}

void DataClock::setDuration(qint64 durationMs)
{
    durationMs = std::max<qint64>(0, durationMs);
    if (durationMs == m_duration)
        {
            return;
        }
    m_duration = durationMs;
    emit durationChanged(m_duration);
    if (position() > m_duration)
        {
            setPosition(m_duration);
        }
}

void DataClock::setRate(int framesPerSecond)
{
    m_ticker.setInterval(std::clamp(1000 / std::max(1, framesPerSecond), 8, 100));
}

void DataClock::play()
{
    if (m_state == QMediaPlayer::PlayingState || m_duration <= 0)
        {
            return;
        }
    // played to the end: again from the start, as a player does
    if (m_start >= m_duration)
        {
            m_start = 0;
        }
    m_playing.start();
    m_ticker.start();
    m_state = QMediaPlayer::PlayingState;
    emit playbackStateChanged(m_state);
}

void DataClock::pause()
{
    if (m_state == QMediaPlayer::PausedState)
        {
            return;
        }
    m_start = position();
    m_ticker.stop();
    m_state = QMediaPlayer::PausedState;
    emit playbackStateChanged(m_state);
    emit positionChanged(m_start);
}

void DataClock::stop()
{
    m_ticker.stop();
    m_start = 0;
    m_state = QMediaPlayer::StoppedState;
    emit playbackStateChanged(m_state);
    emit positionChanged(m_start);
}

void DataClock::setPosition(qint64 positionMs)
{
    m_start = std::clamp<qint64>(positionMs, 0, m_duration);
    if (m_state == QMediaPlayer::PlayingState)
        {
            m_playing.restart();
        }
    emit positionChanged(m_start);
}

void DataClock::tick()
{
    const qint64 now = position();
    emit positionChanged(now);
    if (now >= m_duration)
        {
            pause();
        }
}

Playback::Playback(QMediaPlayer *player, QObject *parent)
    : QObject(parent)
    , m_player(player)
    , m_clock(new DataClock(this))
{
    // each passes on what it says only while it is the one playing
    connect(m_player, &QMediaPlayer::positionChanged, this, [this](qint64 at) { if (!m_useClock) emit positionChanged(at); });
    connect(m_player, &QMediaPlayer::durationChanged, this, [this](qint64 length) { if (!m_useClock) emit durationChanged(length); });
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state)
    {
        if (!m_useClock)
            {
                emit playbackStateChanged(state);
            }
    });
    connect(m_clock, &DataClock::positionChanged, this, [this](qint64 at) { if (m_useClock) emit positionChanged(at); });
    connect(m_clock, &DataClock::durationChanged, this, [this](qint64 length) { if (m_useClock) emit durationChanged(length); });
    connect(m_clock, &DataClock::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state)
    {
        if (m_useClock)
            {
                emit playbackStateChanged(state);
            }
    });
}

void Playback::setClock(bool clock)
{
    if (clock == m_useClock)
        {
            return;
        }
    // the one let go stops where it is
    if (m_useClock)
        {
            m_clock->stop();
        }
    else
        {
            m_player->pause();
        }
    m_useClock = clock;
    emit durationChanged(duration());
    emit positionChanged(position());
    emit playbackStateChanged(playbackState());
}

void Playback::play()
{
    m_useClock ? m_clock->play() : m_player->play();
}

void Playback::pause()
{
    m_useClock ? m_clock->pause() : m_player->pause();
}

void Playback::setPosition(qint64 positionMs)
{
    m_useClock ? m_clock->setPosition(positionMs) : m_player->setPosition(positionMs);
}
