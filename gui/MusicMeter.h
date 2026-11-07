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
        // A rolling loudness reference keeps compressed/loud tracks from pinning
        // all four zones. Typical energy sits at half scale; stronger peaks fill it.
        if (rms > 0.003) {
            m_history[m_historyPosition] = rms;
            m_historyPosition = (m_historyPosition + 1) % m_history.size();
            m_historyCount = std::min(m_historyCount + 1, m_history.size());
            if (m_historyCount == 1 || ++m_referenceTicks >= 10) {
                auto sorted = m_history;
                const auto index = (m_historyCount - 1) * 7 / 10;
                std::nth_element(sorted.begin(), sorted.begin() + index, sorted.begin() + m_historyCount);
                m_reference = std::max(0.02, sorted[index]);
                m_referenceTicks = 0;
            }
        }
        m_targetLevel = rms > 0.003
            ? std::clamp(0.5 * std::pow(rms / m_reference, 1.4), 0.0, 1.0) : 0;
        m_sinceBeat += seconds;
        const double onset = std::max(0.0, rms - m_previousRms);
        const double threshold = std::max(0.003, m_fluxMean * 2.2);
        if (onset < threshold * 0.5) m_armed = true;
        if (m_armed && m_sinceBeat >= 0.22 && onset > threshold && rms > std::max(0.012, m_mean * 0.9)) {
            if (m_haveLastBeat && m_sinceBeat <= 1.5) {
                m_intervals[m_intervalPosition] = m_sinceBeat;
                m_intervalPosition = (m_intervalPosition + 1) % m_intervals.size();
                m_intervalCount = std::min(m_intervalCount + 1, m_intervals.size());
            } else {
                m_intervalCount = m_intervalPosition = 0;
            }
            // One large, immediate color change per onset is easier to read than
            // small blended hue shifts. Meter brightness still fades independently.
            m_hue = std::fmod(m_hue + 120, 360.0);
            ++m_beats;
            m_sinceBeat = 0;
            m_haveLastBeat = true;
            m_armed = false;
        }
        m_fluxMean += (1 - std::exp(-seconds / 0.3)) * (onset - m_fluxMean);
        m_mean += (1 - std::exp(-seconds / 0.5)) * (rms - m_mean);
        m_previousRms = rms;
    }

    void silence() {
        m_targetLevel = 0;
        m_intervalCount = m_intervalPosition = 0;
        m_haveLastBeat = false;
    }
    void advance(double seconds) {
        seconds = std::clamp(seconds, 0.0, 1.0);
        const double timeConstant = m_targetLevel > m_level ? 0.02 : 0.24;
        m_level += (1 - std::exp(-seconds / timeConstant)) * (m_targetLevel - m_level);
        if (m_level < 0.0001) m_level = 0;
    }
    std::array<double, 4> levels() const {
        std::array<double, 4> result{};
        for (int zone = 0; zone < 4; ++zone) result[zone] = std::clamp(m_level * 4 - zone, 0.0, 1.0);
        return result;
    }
    double hueOffset() const { return std::fmod(m_hue, 360.0); }
    unsigned beats() const { return m_beats; }
    double beatRate() const {
        if (m_intervalCount < 2) return 0;
        auto sorted = m_intervals;
        std::sort(sorted.begin(), sorted.begin() + m_intervalCount);
        const double period = sorted[m_intervalCount / 2];
        return m_sinceBeat <= std::max(2.0, period * 3) ? 60 / period : 0;
    }
private:
    std::array<unsigned char, 4> m_frame{};
    unsigned m_bytes = 0, m_frames = 0, m_beats = 0;
    double m_sumSquares = 0, m_mean = 0, m_previousRms = 0, m_sinceBeat = 1;
    double m_level = 0, m_targetLevel = 0, m_hue = 0, m_fluxMean = 0;
    std::array<double, 300> m_history{};
    std::size_t m_historyPosition = 0, m_historyCount = 0;
    unsigned m_referenceTicks = 0;
    double m_reference = 0.02;
    std::array<double, 5> m_intervals{};
    std::size_t m_intervalPosition = 0, m_intervalCount = 0;
    bool m_armed = true, m_haveLastBeat = false;
};
