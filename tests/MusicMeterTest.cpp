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
    check(medium.levels()[0] == 1 && medium.levels()[1] == 1 && medium.levels()[3] == 0,
          "Medium audio must fill from left to right");
    check(loud.levels()[3] > 0.99, "Loud audio must fill all zones");
    for (int i = 1; i < 4; ++i)
        check(medium.levels()[i] <= medium.levels()[i - 1], "Meter zones must stay ordered");
    check(constant(0.2).beats() == 1, "A sustained sound must not repeatedly trigger color changes");
    check(pulses(25).beats() == 20 && pulses(50).beats() == 10,
          "Faster beats must produce more color changes");
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
    check(whole.levels()[2] > 0.9, "Stereo energy must not cancel opposite-phase channels");
    return failures ? 1 : 0;
}
