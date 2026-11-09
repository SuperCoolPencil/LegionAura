#include "MusicSync.h"
#include "RibbonFrame.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QMetaObject>

namespace {
const QString service = "org.gnome.Shell.Extensions.NowPlayingCard";
const QString path = "/org/gnome/Shell/Extensions/NowPlayingCard";
}

MusicSync::MusicSync(LegionAura &keyboard, QObject *parent) : QObject(parent), m_keyboard(keyboard)
{
    m_timer.setInterval(50);
    connect(&m_timer, &QTimer::timeout, this, &MusicSync::tick);
    QDBusConnection::sessionBus().connect(service, path, service, "Frame", this,
                                        SLOT(onRibbonFrame(QString)));
    auto *watcher = new QDBusServiceWatcher(service, QDBusConnection::sessionBus(),
                                          QDBusServiceWatcher::WatchForRegistration, this);
    connect(watcher, &QDBusServiceWatcher::serviceRegistered, this, [this] { requestFrame(); });
}

MusicSync::~MusicSync() { stop(); }

void MusicSync::start(int brightness)
{
    stop();
    m_brightness = brightness;
    m_target.fill(QColor(Qt::black));
    m_current.fill(QColor(Qt::black));
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
    m_elapsed.invalidate();
    m_timer.start();
    tick();
    emit colorsChanged();
    requestFrame();
}

void MusicSync::requestFrame()
{
    if (!m_running) return;
    const auto generation = m_generation;
    const auto message = QDBusMessage::createMethodCall(service, path, service, "GetFrame");
    auto *call = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message, 1000), this);
    connect(call, &QDBusPendingCallWatcher::finished, this, [this, generation](QDBusPendingCallWatcher *finished) {
        const QDBusPendingReply<QString> reply = *finished;
        finished->deleteLater();
        if (!m_running || generation != m_generation || reply.isError()) return;
        onRibbonFrame(reply.value());
    });
}

void MusicSync::onRibbonFrame(const QString &json)
{
    if (!m_running) return;
    const auto colors = decodeRibbonFrame(json);
    if (!colors) return;
    m_target = *colors;
    m_elapsed.start();
    tick();
}

void MusicSync::stop()
{
    m_running = false;
    ++m_generation;
    m_timer.stop();
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
    // A missing/stopped extension must never leave old lighting stuck on.
    auto colors = m_target;
    if (!m_elapsed.isValid() || m_elapsed.elapsed() > 1000)
        colors.fill(QColor(Qt::black));
    LAParams params{LAEffect::Static, 1, static_cast<uint8_t>(m_brightness), {}, LAWaveDir::None};
    for (int i = 0; i < 4; ++i)
        params.zones[i] = {static_cast<uint8_t>(colors[i].red()),
                           static_cast<uint8_t>(colors[i].green()),
                           static_cast<uint8_t>(colors[i].blue())};
    const bool changed = colors != m_current;
    m_current = colors;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pending = params;
    }
    m_ready.notify_one();
    if (changed) emit colorsChanged();
}
