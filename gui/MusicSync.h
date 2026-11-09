#pragma once
#include "legionaura.h"
#include <QObject>
#include <QColor>
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
    void start(int brightness);
    void stop();
    void setBrightness(int brightness) { m_brightness = brightness; }
    std::array<QColor, 4> colors() const { return m_current; }
signals:
    void colorsChanged();
    void failed(const QString &message);
private slots:
    void onRibbonFrame(const QString &json);
private:
    void requestFrame();
    void tick();
    LegionAura &m_keyboard;
    QTimer m_timer;
    QElapsedTimer m_elapsed;
    std::thread m_writer;
    std::mutex m_mutex;
    std::condition_variable m_ready;
    std::optional<LAParams> m_pending;
    std::array<QColor, 4> m_current{}, m_target{};
    bool m_running = false, m_stopping = true;
    int m_brightness = 2;
    quint64 m_generation = 0;
};
