#pragma once

#include <QElapsedTimer>
#include <QMediaPlayer>
#include <QObject>
#include <QTimer>
#include <QUrl>

// The time of a project with no video (VideoBackground): played, paused and moved through as
// a video would be, its length the data's. Speaks as QMediaPlayer does, in milliseconds.
class DataClock : public QObject
{
    Q_OBJECT

public:
    explicit DataClock(QObject *parent = nullptr);

    qint64 position() const;
    qint64 duration() const { return m_duration; }
    QMediaPlayer::PlaybackState playbackState() const { return m_state; }
    void setDuration(qint64 durationMs);
    // pictures a second while playing
    void setRate(int framesPerSecond);

public slots:
    void play();
    void pause();
    void stop();
    void setPosition(qint64 positionMs);

signals:
    void positionChanged(qint64 positionMs);
    void durationChanged(qint64 durationMs);
    void playbackStateChanged(QMediaPlayer::PlaybackState state);

private:
    void tick();

    qint64 m_duration{0};
    qint64 m_start{0};          // the position when it started playing, or where it stands
    QElapsedTimer m_playing;
    QTimer m_ticker;
    QMediaPlayer::PlaybackState m_state{QMediaPlayer::StoppedState};
};

// What the window plays: the video's player, or the data's clock when the project has a
// background instead (setClock). One face for both, so the timeline, the slider, the music
// and the preview do not ask which.
class Playback : public QObject
{
    Q_OBJECT

public:
    Playback(QMediaPlayer *player, QObject *parent = nullptr);

    // true: the clock plays, the player is left alone
    void setClock(bool clock);
    bool isClock() const { return m_useClock; }
    // A video whose pictures change their settings inside the stream (Libav::changesSettings):
    // a seek back opens it again first, which is the one way the player shows it right then.
    void setReopenGoingBack(bool reopen) { m_reopenGoingBack = reopen; }
    DataClock *clock() { return m_clock; }

    // while the video is opened again: where it is going
    qint64 position() const { return m_useClock ? m_clock->position() : m_reopenedAt >= 0 ? m_reopenedAt : m_player->position(); }
    qint64 duration() const { return m_useClock ? m_clock->duration() : m_player->duration(); }
    QMediaPlayer::PlaybackState playbackState() const { return m_useClock ? m_clock->playbackState() : m_player->playbackState(); }

public slots:
    void play();
    void pause();
    void setPosition(qint64 positionMs);

signals:
    void positionChanged(qint64 positionMs);
    void durationChanged(qint64 durationMs);
    void playbackStateChanged(QMediaPlayer::PlaybackState state);

private:
    QMediaPlayer *m_player;
    DataClock *m_clock;
    bool m_useClock{false};
    // The video played to its end: the player's decoder then gives a broken picture after a seek,
    // so the next seek opens the video again first and goes there once it is loaded.
    bool m_ended{false};
    qint64 m_reopenedAt{-1};   // where to go once opened again; -1: not opening
    QUrl m_reopenedSource;     // the video opened again
    bool m_reopenGoingBack{false};
};
