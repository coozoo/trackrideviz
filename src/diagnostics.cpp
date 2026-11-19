#include "diagnostics.h"

#include "icons.h"
#include "video/libav.h"

#include <QApplication>
#include <QAudioDevice>
#include <QClipboard>
#include <QFontDatabase>
#include <QDateTime>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLibraryInfo>
#include <QMediaDevices>
#include <QMutex>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>

#include <deque>

namespace
{
const size_t kKeptLines = 5000;

struct Log
{
    QMutex mutex;
    std::deque<QString> lines;
    qint64 count{0};        // lines ever added; the first kept one is count - lines.size()
    QtMessageHandler previous{nullptr};
};

Log &log()
{
    static Log instance;
    return instance;
}

void add(const QString &line)
{
    Log &l = log();
    QMutexLocker lock(&l.mutex);
    l.lines.push_back(QTime::currentTime().toString("HH:mm:ss.zzz") + "  " + line);
    ++l.count;
    while (l.lines.size() > kKeptLines)
        {
            l.lines.pop_front();
        }
}

void handler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    static const char *const kinds[] = {"debug", "warning", "critical", "fatal", "info"};
    const char *kind = type >= 0 && type < int(std::size(kinds)) ? kinds[type] : "?";
    const QString category = context.category && qstrcmp(context.category, "default") != 0
                             ? QString::fromLatin1(context.category) + ": " : QString();
    add(QString("[%1] %2%3").arg(QLatin1String(kind), category, message));
    if (log().previous)
        {
            log().previous(type, context, message);
        }
}
}

void DiagnosticLog::install()
{
    log().previous = qInstallMessageHandler(handler);
}

void DiagnosticLog::note(const QString &area, const QString &text)
{
    add(QString("[%1] %2").arg(area, text));
}

QStringList DiagnosticLog::linesSince(qint64 from, qint64 *count)
{
    Log &l = log();
    QMutexLocker lock(&l.mutex);
    *count = l.count;
    const qint64 first = l.count - qint64(l.lines.size());
    QStringList out;
    for (qint64 i = std::max(from, first); i < l.count; ++i)
        {
            out << l.lines.at(size_t(i - first));
        }
    return out;
}

void DiagnosticLog::clear()
{
    Log &l = log();
    QMutexLocker lock(&l.mutex);
    l.lines.clear();
}

DiagnosticsPanel::DiagnosticsPanel(std::function<QString()> state, QWidget *parent)
    : QWidget(parent)
    , m_state(std::move(state))
{
    m_environment = new QLabel(environment(), this);
    m_environment->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_environment->setWordWrap(true);
    m_stateLabel = new QLabel(this);
    m_stateLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_stateLabel->setWordWrap(true);
    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_log->setMaximumBlockCount(int(kKeptLines));
    m_log->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    QPushButton *copy = new QPushButton(Icons::named("edit-copy"), tr("Copy all"), this);
    copy->setToolTip(tr("The start-up details, the players' state and the log, to paste into a report"));
    connect(copy, &QPushButton::clicked, this, [this]()
    {
        QApplication::clipboard()->setText(m_environment->text() + "\n\n" + m_stateLabel->text() + "\n\n"
                                           + m_log->toPlainText());
    });
    QPushButton *clearButton = new QPushButton(Icons::named("edit-clear"), tr("Clear"), this);
    connect(clearButton, &QPushButton::clicked, this, [this]()
    {
        DiagnosticLog::clear();
        m_log->clear();
    });
    QHBoxLayout *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    buttons->addWidget(copy);
    buttons->addWidget(clearButton);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(m_environment);
    layout->addWidget(m_stateLabel);
    layout->addWidget(m_log, 1);
    layout->addLayout(buttons);

    m_timer = new QTimer(this);
    m_timer->setInterval(300);
    connect(m_timer, &QTimer::timeout, this, &DiagnosticsPanel::refresh);
}

QString DiagnosticsPanel::environment()
{
    QStringList lines;
    lines << tr("Qt %1 (built with %2), display: %3").arg(QString::fromLatin1(qVersion()), QStringLiteral(QT_VERSION_STR),
            QGuiApplication::platformName());
    const QByteArray appImage = qgetenv("APPIMAGE");
    lines << (appImage.isEmpty() ? tr("Not run from an AppImage") : tr("AppImage: %1").arg(QString::fromLocal8Bit(appImage)));
    // what decides where plugins and media come from
    for (const char *name : {"QT_PLUGIN_PATH", "QT_MEDIA_BACKEND", "GST_PLUGIN_PATH", "GST_PLUGIN_SYSTEM_PATH",
                             "GST_PLUGIN_SCANNER", "GST_REGISTRY", "GST_REGISTRY_FORK", "LIBVA_DRIVER_NAME",
                             // and how text and icons look
                             "QT_QPA_PLATFORMTHEME", "XDG_CURRENT_DESKTOP", "QT_SCALE_FACTOR", "QT_FONT_DPI",
                             "FONTCONFIG_FILE"})
        {
            const QByteArray value = qgetenv(name);
            if (!value.isEmpty())
                {
                    lines << QString("%1=%2").arg(QLatin1String(name), QString::fromLocal8Bit(value));
                }
        }
    lines << tr("Qt plugins: %1").arg(QLibraryInfo::path(QLibraryInfo::PluginsPath));
    // whether this build has the video engine of its own, and which libraries it runs on
    lines << (Libav::available() ? tr("Video engine: %1").arg(Libav::version())
              : tr("Video engine: the ffmpeg program only (built without FFmpeg's libraries)"));
    // where sound can go: none found means silence, whatever the players say
    QStringList outputs;
    for (const QAudioDevice &device : QMediaDevices::audioOutputs())
        outputs << device.description();
    lines << (outputs.isEmpty() ? tr("Sound devices: none found") : tr("Sound devices: %1").arg(outputs.join(", ")));
    // the font asked for and the one fontconfig gave: a desktop theme Qt could not load shows here
    const QFont font = QApplication::font();
    const QFontInfo got(font);
    const QScreen *screen = QGuiApplication::primaryScreen();
    lines << tr("Font: %1 %2 pt, drawn with %3 %4 pt; %5 dpi, scale %6; icon theme: %7")
                 .arg(font.family())
                 .arg(font.pointSizeF())
                 .arg(got.family())
                 .arg(got.pointSizeF())
                 .arg(screen ? screen->logicalDotsPerInch() : 0.0)
                 .arg(screen ? screen->devicePixelRatio() : 1.0)
                 .arg(QIcon::themeName().isEmpty() ? tr("none") : QIcon::themeName());
    return lines.join('\n');
}

void DiagnosticsPanel::refresh()
{
    if (m_state)
        {
            m_stateLabel->setText(m_state());
        }
    qint64 count = 0;
    const QStringList lines = DiagnosticLog::linesSince(m_shown, &count);
    m_shown = count;
    if (!lines.isEmpty())
        {
            m_log->appendPlainText(lines.join('\n'));
        }
}

void DiagnosticsPanel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    refresh();
    m_timer->start();
}

void DiagnosticsPanel::hideEvent(QHideEvent *event)
{
    // nothing to do while nobody looks; the log goes on filling
    m_timer->stop();
    QWidget::hideEvent(event);
}
