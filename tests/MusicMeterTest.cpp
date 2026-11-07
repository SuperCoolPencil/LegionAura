#include "MusicMeter.h"
#include <iostream>
#include <string>

static int failures = 0;
static void check(bool condition, const char *message) {
    if (!condition) { std::cerr << message << '\n'; ++failures; }
}
static MusicMeter constant(double rms) {
    MusicMeter meter;
    for (int i = 0; i < 100; ++i) { meter.observeRms(rms, 0.01); meter.advance(0.01); }
    return meter;
}
static MusicMeter pulses(int spacing) {
    MusicMeter meter;
    for (int i = 0; i < 500; ++i) {
        meter.observeRms(i % spacing < 4 ? 0.4 : 0.005, 0.01);
        meter.advance(0.01);
    }
    return meter;
}
int main() {
    check(constant(0).levels() == std::array<double, 4>{}, "Silence must be fully dark");
    check(constant(0.001).beats() == 0, "Low noise must not trigger beat colors");
    auto quiet = constant(0.01), medium = constant(0.06), loud = constant(0.8);
    check(quiet.levels()[0] > 0 && quiet.levels()[1] == 0, "Quiet audio must light only the first zone");
    check(medium.levels()[0] > 0.99 && medium.levels()[1] > 0.99 && medium.levels()[2] == 0,
          "Medium audio must fill from left to right");
    check(loud.levels()[1] > 0.99 && loud.levels()[3] == 0,
          "Sustained loud/compressed playback must not pin the meter full");
    auto peak = constant(0.2);
    peak.observeRms(0.8, 0.01); peak.advance(0.2);
    check(peak.levels()[3] > 0.99, "Strong peaks above recent loudness must still fill all zones");
    auto compressed = constant(0.65);
    compressed.observeRms(0.7, 0.01); compressed.advance(0.1);
    check(compressed.levels()[2] > 0 && compressed.levels()[3] == 0,
          "Small changes in a compressed track must move the meter without saturating it");
    check(compressed.beats() == 2, "A modest attack in loud compressed audio must still change color");
    for (int i = 1; i < 4; ++i)
        check(medium.levels()[i] <= medium.levels()[i - 1], "Meter zones must stay ordered");
    check(constant(0.2).beats() == 1, "A sustained sound must not repeatedly trigger color changes");
    check(pulses(25).beats() == 20 && pulses(50).beats() == 10,
          "Faster beats must produce more color changes");
    check(std::abs(pulses(50).beatRate() - 120) < 1 && std::abs(pulses(25).beatRate() - 240) < 1,
          "Beat-rate estimates must distinguish slow and fast music");
    MusicMeter distinct;
    distinct.observeRms(0.2, 0.01);
    check(distinct.hueOffset() == 120, "Each onset must produce an immediate, clearly distinct hue");
    for (int i = 0; i < 10; ++i) distinct.advance(0.01);
    check(distinct.hueOffset() == 120, "Color must stay fixed between detected beats");
    auto stopped = pulses(50);
    stopped.silence();
    check(stopped.beatRate() == 0, "Missing audio must clear the displayed beat estimate");
    loud.silence(); loud.advance(0.05);
    check(loud.levels()[0] > 0, "Release should fade rather than cut immediately");
    for (int i = 0; i < 40; ++i) loud.advance(0.1);
    check(loud.levels() == std::array<double, 4>{}, "Stopped audio must fade completely to black");
    // Opposite-phase stereo must not cancel. Feed the same PCM in fragmented chunks.
    std::string pcm;
    for (int i = 0; i < 4410; ++i) pcm.append("\x00\x40\x00\xc0", 4);
    MusicMeter whole, fragmented;
    whole.process(pcm.data(), pcm.size());
    for (std::size_t i = 0; i < pcm.size(); i += 7)
        fragmented.process(pcm.data() + i, std::min(std::size_t(7), pcm.size() - i));
    whole.advance(0.5); fragmented.advance(0.5);
    check(whole.levels() == fragmented.levels() && whole.beats() == fragmented.beats(),
          "PCM chunk boundaries and partial samples must not affect analysis");
    check(whole.levels()[1] > 0.9, "Stereo energy must not cancel opposite-phase channels");
    return failures ? 1 : 0;
}
