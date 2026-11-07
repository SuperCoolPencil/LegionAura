#include "MusicSync.h"
#include <QMetaObject>
#include <algorithm>
#include <cmath>
#include <cstring>

MusicSync::MusicSync(LegionAura &keyboard, QObject *parent) : QObject(parent), m_keyboard(keyboard)
{
    // Fallback for fades during silence; incoming audio updates immediately.
    m_timer.setInterval(16);
    connect(&m_timer, &QTimer::timeout, this, &MusicSync::tick);
    connect(&m_audio, &QProcess::readyReadStandardOutput, this, [this] {
        m_audioBytes += m_audio.readAllStandardOutput();
        const QByteArray data = m_audioBytes.left(m_audioBytes.size() & ~qsizetype(1));
        m_audioBytes.remove(0, data.size());
        std::array<double, 4> sums{};
        int count = 0;
        for (qsizetype i = 0; i + 1 < data.size(); i += 2) {
            const auto lo = static_cast<unsigned char>(data[i]);
            const auto hi = static_cast<unsigned char>(data[i + 1]);
            const auto sample = static_cast<qint16>(static_cast<quint16>(lo | (hi << 8)));
            const double input = sample / 32768.0;
            // Nested low-pass filters split audio into four complementary bands.
            constexpr double cutoffs[] = {180.0, 700.0, 3000.0};
            for (int band = 0; band < 3; ++band) {
                const double alpha = 1.0 - std::exp(-2.0 * 3.141592653589793 * cutoffs[band] / 44100.0);
                m_filters[band] += alpha * (input - m_filters[band]);
            }
            const double bands[] = {m_filters[0], m_filters[1] - m_filters[0],
                                    m_filters[2] - m_filters[1], input - m_filters[2]};
            for (int band = 0; band < 4; ++band) sums[band] += bands[band] * bands[band];
            ++count;
        }
        if (count)
            for (int band = 0; band < 4; ++band)
                m_levels[band] = std::max(m_levels[band], std::sqrt(sums[band] / count));
        if (count && m_running) tick();
    });
    connect(&m_audio, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (!m_running) return;
        stop();
        emit failed("Music Sync: audio capture failed. Install PulseAudio utilities and check your audio service.");
    });
    connect(&m_audio, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int, QProcess::ExitStatus) {
        if (!m_running) return;
        stop();
        emit failed("Music Sync: audio capture stopped. Check your audio output and press Apply to retry.");
    });
}
MusicSync::~MusicSync() { stop(); }

void MusicSync::start(int brightness, const std::array<LAColor, 4> &colors)
{
    stop();
    // PulseAudio and pipewire-pulse expose the default output's monitor as this source.
    QProcess query;
    query.start("pactl", {"get-default-sink"});
    if (!query.waitForFinished(3000) || query.exitCode() != 0) {
        emit failed("Music Sync: could not find the default audio output. Install pactl and check your audio service.");
        return;
    }
    const QString sink = QString::fromUtf8(query.readAllStandardOutput()).trimmed();
    if (sink.isEmpty()) {
        emit failed("Music Sync: no default audio output found.");
        return;
    }
    m_brightness = brightness;
    m_colors = colors;
    m_levels.fill(0);
    m_averages.fill(0);
    m_envelopes.fill(0);
    m_filters.fill(0);
    m_audioBytes.clear();
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
    m_audio.start("parec", {"--device=" + sink + ".monitor", "--format=s16le", "--rate=44100", "--channels=1", "--latency-msec=1"});
    m_elapsed.start();
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
    const double steps = std::max(0.001, m_elapsed.nsecsElapsed() / 25000000.0);
    m_elapsed.restart();
    // Fast attack, slow release: each band breathes independently without flicker.
    for (int i = 0; i < 4; ++i) {
        const double retain = std::pow(0.98, steps);
        m_averages[i] = retain * m_averages[i] + (1.0 - retain) * m_levels[i];
        const double relative = m_levels[i] / std::max(0.012, m_averages[i] * 2.2);
        const double gate = std::clamp(m_levels[i] / 0.008, 0.0, 1.0);
        const double target = std::clamp(relative, 0.0, 1.0) * gate;
        const double blend = 1.0 - std::pow(target > m_envelopes[i] ? 0.25 : 0.88, steps);
        m_envelopes[i] += blend * (target - m_envelopes[i]);
        m_levels[i] = 0;
    }
    LAParams params{LAEffect::Static, 1, static_cast<uint8_t>(m_brightness), {}, LAWaveDir::None};
    for (int i = 0; i < 4; ++i) {
        const double intensity = 0.04 + 0.96 * m_envelopes[i];
        params.zones[i] = {static_cast<uint8_t>(m_colors[i].r * intensity),
                           static_cast<uint8_t>(m_colors[i].g * intensity),
                           static_cast<uint8_t>(m_colors[i].b * intensity)};
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
