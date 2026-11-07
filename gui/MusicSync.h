#pragma once
#include "legionaura.h"
#include "MusicMeter.h"
#include <QObject>
#include <QColor>
#include <QProcess>
#include <QTimer>
#include <QElapsedTimer>
#include <array>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

class MusicSync : public QObject {
    Q_OBJECT
public:
    explicit MusicSync(LegionAura &keyboard, QObject *parent = nullptr);
    ~MusicSync() override;
    void start(int brightness, const std::array<LAColor, 4> &colors);
    void stop();
    void setBrightness(int brightness) { m_brightness = brightness; }
    std::array<QColor, 4> colors() const { return m_current; }
signals:
    void colorsChanged();
    void failed(const QString &message);
    void started();
private:
    void tick();
    LegionAura &m_keyboard;
    QProcess m_audio;
    QTimer m_timer;
    QElapsedTimer m_elapsed;
    std::thread m_writer;
    std::mutex m_mutex;
    std::condition_variable m_ready;
    std::optional<LAParams> m_pending;
    std::array<LAColor, 4> m_colors{};
    std::array<QColor, 4> m_current{};
    MusicMeter m_meter;
    QElapsedTimer m_audioAge;
    QString m_audioError;
    bool m_running = false, m_stopping = true;
    int m_brightness = 2;
    quint64 m_generation = 0;
};
