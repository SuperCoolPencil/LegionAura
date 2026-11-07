#include "MusicSync.h"
#include <QMetaObject>
#include <algorithm>

MusicSync::MusicSync(LegionAura &keyboard, QObject *parent) : QObject(parent), m_keyboard(keyboard)
{
    // Fallback for fades during silence; incoming audio updates immediately.
    m_timer.setInterval(16);
    connect(&m_timer, &QTimer::timeout, this, &MusicSync::tick);
    m_current.fill(Qt::black);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_audio, &QProcess::readyReadStandardOutput, this, [this] {
        const auto data = m_audio.readAllStandardOutput();
        if (!m_running || data.isEmpty()) return;
        m_audioAge.restart();
        m_meter.process(data.constData(), std::size_t(data.size()));
        tick();
    });
    connect(&m_audio, &QProcess::readyReadStandardError, this, [this] {
        m_audioError = (m_audioError + QString::fromUtf8(m_audio.readAllStandardError())).right(2048);
    });
    connect(&m_audio, &QProcess::started, this, [this] { if (m_running) emit started(); });
    connect(&m_audio, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (!m_running) return;
        const auto error = m_audio.errorString();
        stop();
        emit failed("Music Sync: " + error + ". Check parec (pulseaudio-utils) and your audio service.");
    });
    connect(&m_audio, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int, QProcess::ExitStatus) {
        if (!m_running) return;
        const auto detail = m_audioError.trimmed();
        stop();
        emit failed("Music Sync: playback capture stopped. " + detail + " Press Apply to retry.");
    });
}
MusicSync::~MusicSync() { stop(); }

void MusicSync::start(int brightness, const std::array<LAColor, 4> &colors)
{
    stop();
    m_brightness = brightness;
    m_colors = colors;
    m_meter = MusicMeter{};
    m_current.fill(Qt::black);
    m_audioError.clear();
    m_lastBeatRate = -1;
    m_running = true;
    m_stopping = false;
    const auto generation = ++m_generation;
    m_writer = std::thread([this, generation] {
        std::optional<LAParams> previous;
        while (true) {
            LAParams params;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_ready.wait(lock, [this] { return m_stopping || m_pending.has_value(); });
                if (m_stopping) return;
                params = *m_pending;
                m_pending.reset();
            }
            bool same = previous && previous->brightness == params.brightness;
            for (int i = 0; same && i < 4; ++i)
                same = previous->zones[i].r == params.zones[i].r && previous->zones[i].g == params.zones[i].g && previous->zones[i].b == params.zones[i].b;
            if (same) continue;
            if (!m_keyboard.apply(params)) {
                QMetaObject::invokeMethod(this, [this, generation] {
                    if (!m_running || generation != m_generation) return;
                    stop();
                    emit failed("Music Sync stopped: keyboard write failed. Reconnect and press Apply.");
                }, Qt::QueuedConnection);
                return;
            }
            previous = params;
        }
    });
    m_audio.setProcessChannelMode(QProcess::SeparateChannels);
    // Explicitly capture playback, never the default microphone.
    m_audio.start("parec", {"--device=@DEFAULT_MONITOR@", "--format=s16le", "--rate=44100",
                           "--channels=2", "--latency-msec=20", "--process-time-msec=10",
                           "--client-name=LegionAura", "--stream-name=Music Sync"});
    m_elapsed.start();
    m_audioAge.start();
    m_timer.start();
}

void MusicSync::stop()
{
    m_running = false;
    ++m_generation;
    m_timer.stop();
    if (m_audio.state() != QProcess::NotRunning) {
        m_audio.kill();
        m_audio.waitForFinished(1000);
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopping = true;
        m_pending.reset();
    }
    m_ready.notify_all();
    if (m_writer.joinable()) m_writer.join();
}

void MusicSync::tick()
{
    if (!m_running) return;
    const double seconds = m_elapsed.nsecsElapsed() / 1000000000.0;
    m_elapsed.restart();
    // An idle/suspended monitor can stop producing PCM entirely.
    if (m_audioAge.elapsed() > 100) m_meter.silence();
    m_meter.advance(seconds);
    const auto levels = m_meter.levels();
    const int beatRate = qRound(m_meter.beatRate());
    if (beatRate != m_lastBeatRate) {
        m_lastBeatRate = beatRate;
        emit beatRateChanged(beatRate);
    }
    LAParams params{LAEffect::Static, 1, static_cast<uint8_t>(m_brightness), {}, LAWaveDir::None};
    for (int i = 0; i < 4; ++i) {
        const QColor base(m_colors[i].r, m_colors[i].g, m_colors[i].b);
        const int hue = (std::max(0, base.hsvHue()) + qRound(m_meter.hueOffset())) % 360;
        const auto color = QColor::fromHsv(hue, base.hsvSaturation(), base.value());
        params.zones[i] = {static_cast<uint8_t>(qRound(color.red() * levels[i])),
                           static_cast<uint8_t>(qRound(color.green() * levels[i])),
                           static_cast<uint8_t>(qRound(color.blue() * levels[i]))};
    }
    for (int i = 0; i < 4; ++i)
        m_current[i] = QColor(params.zones[i].r, params.zones[i].g, params.zones[i].b);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pending = params;
    }
    m_ready.notify_one();
    emit colorsChanged();
}
