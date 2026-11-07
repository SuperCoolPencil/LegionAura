#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

// Stereo s16le at 44.1 kHz, analysed in fixed 10 ms windows. Capture chunk
// boundaries cannot change the level or beat detector's timing.
class MusicMeter {
public:
    void process(const char *data, std::size_t size) {
        for (std::size_t i = 0; i < size; ++i) {
            m_frame[m_bytes++] = static_cast<unsigned char>(data[i]);
            if (m_bytes != 4) continue;
            for (int channel = 0; channel < 2; ++channel) {
                const int offset = channel * 2;
                const auto word = unsigned(m_frame[offset]) | (unsigned(m_frame[offset + 1]) << 8);
                const int sample = word < 32768 ? int(word) : int(word) - 65536;
                const double value = sample / 32768.0;
                m_sumSquares += value * value;
            }
            m_bytes = 0;
            if (++m_frames == 441) {
                observeRms(std::sqrt(m_sumSquares / 882), 0.01);
                m_frames = 0;
                m_sumSquares = 0;
            }
        }
    }

    void observeRms(double rms, double seconds) {
        if (!std::isfinite(rms) || seconds <= 0) return;
        rms = std::clamp(rms, 0.0, 1.0);
        // A -48 to -6 dBFS meter keeps ordinary music useful without normalising
        // quiet and loud passages to the same level. Silence is fully dark.
        m_targetLevel = rms > 0 ? std::clamp((20 * std::log10(rms) + 48) / 42, 0.0, 1.0) : 0;
        m_sinceBeat += seconds;
        const double threshold = std::max(0.012, m_mean * 1.6);
        if (rms < m_mean * 1.15 || rms < m_previousRms * 0.85) m_armed = true;
        if (m_armed && m_sinceBeat >= 0.16 && rms > threshold && rms > m_previousRms * 1.2) {
            m_targetHue += 55;
            ++m_beats;
            m_sinceBeat = 0;
            m_armed = false;
        }
        m_mean += (1 - std::exp(-seconds / 0.5)) * (rms - m_mean);
        m_previousRms = rms;
    }

    void silence() { m_targetLevel = 0; }
    void advance(double seconds) {
        seconds = std::clamp(seconds, 0.0, 1.0);
        const double timeConstant = m_targetLevel > m_level ? 0.02 : 0.24;
        m_level += (1 - std::exp(-seconds / timeConstant)) * (m_targetLevel - m_level);
        if (m_level < 0.0001) m_level = 0;
        m_hue += (1 - std::exp(-seconds / 0.08)) * (m_targetHue - m_hue);
    }
    std::array<double, 4> levels() const {
        std::array<double, 4> result{};
        for (int zone = 0; zone < 4; ++zone) result[zone] = std::clamp(m_level * 4 - zone, 0.0, 1.0);
        return result;
    }
    double hueOffset() const { return std::fmod(m_hue, 360.0); }
    unsigned beats() const { return m_beats; }
private:
    std::array<unsigned char, 4> m_frame{};
    unsigned m_bytes = 0, m_frames = 0, m_beats = 0;
    double m_sumSquares = 0, m_mean = 0, m_previousRms = 0, m_sinceBeat = 1;
    double m_level = 0, m_targetLevel = 0, m_hue = 0, m_targetHue = 0;
    bool m_armed = true;
};
