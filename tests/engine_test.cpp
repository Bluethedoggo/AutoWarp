// Stand-alone test of the warp engine (no JUCE needed).
// Build:  g++ -O2 -std=c++17 -I../Source engine_test.cpp ../Source/WarpEngine.cpp -o engine_test
#include "WarpEngine.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>

using namespace dogside;

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf ("  FAIL: %s\n", msg); ++failures; } else std::printf ("  ok:   %s\n", msg); } while (0)

// synthetic drum-ish hit: a decaying sine burst + noise
static void addHit (std::vector<float>& x, size_t pos, double sr, float freq, float amp)
{
    std::mt19937 rng ((unsigned) pos);
    std::uniform_real_distribution<float> nd (-1.f, 1.f);
    const size_t len = (size_t) (0.12 * sr);
    for (size_t i = 0; i < len && pos + i < x.size(); ++i)
    {
        const double t = (double) i / sr;
        const double env = std::exp (-t * 38.0);
        x[pos + i] += (float) (amp * env * (0.8 * std::sin (2.0 * M_PI * freq * t) + 0.2 * nd (rng)));
    }
}

int main()
{
    const double sr = 44100.0;
    const double bpm = 120.0, beats = 8.0;
    const size_t n = (size_t) (beats * 60.0 / bpm * sr);

    AudioData a;
    a.sampleRate = sr;
    a.ch.assign (2, std::vector<float> (n, 0.0f));

    // hits on every 8th note, but each one is off by up to +-18 ms
    std::mt19937 rng (7);
    std::uniform_real_distribution<double> jit (-0.018, 0.018);
    std::vector<double> truth;
    for (int k = 0; k < 16; ++k)
    {
        double t = k * 0.25 + (k == 0 ? 0.0 : jit (rng));
        if (k == 0) t = 0.004;
        const size_t pos = (size_t) (t * sr);
        addHit (a.ch[0], pos, sr, k % 2 ? 900.f : 120.f, 0.7f);
        truth.push_back (t);
    }
    a.ch[1] = a.ch[0];

    std::printf ("[auto warp]\n");
    const auto aw = autoWarp (a, 0.5);
    std::printf ("  detected: %zu markers, loopBeats=%d, bpm=%.2f, start=%.0f end=%.0f\n",
                 aw.markers.size(), aw.loopBeats, aw.bpm, aw.startSample, aw.endSample);
    CHECK (aw.loopBeats == 8, "loop length detected as 8 beats");
    CHECK (std::abs (aw.bpm - 120.0) < 0.5, "tempo ~120 bpm");
    CHECK (aw.markers.size() >= 13 && aw.markers.size() <= 16, "about one marker per hit");

    // every marker should be within 4 ms of a real hit
    int near = 0;
    for (const auto& m : aw.markers)
        for (double t : truth)
            if (std::abs (m.sample / sr - t) < 0.004) { ++near; break; }
    CHECK (near >= (int) aw.markers.size() - 1, "markers sit on the transients (<4 ms)");

    std::printf ("[quantize 1/8, strength 100%%, render @120]\n");
    WarpSettings ws;
    ws.loopBeats = aw.loopBeats; ws.strength = 1.0; ws.gridBeats = 0.5; ws.swing = 0.0;
    ws.startSample = aw.startSample; ws.endSample = aw.endSample; ws.sourceRate = sr;
    const auto plan = computePlan (aw.markers, (double) n, ws);

    bool monotonic = true;
    for (size_t i = 1; i < plan.points.size(); ++i)
        if (plan.points[i].targetBeat < plan.points[i - 1].targetBeat) monotonic = false;
    CHECK (plan.valid() && monotonic, "plan is valid and monotonic");

    RenderResult rr;
    const auto t0 = std::chrono::steady_clock::now();
    const bool ok = renderWarp (a, plan, sr, 120.0, StretchMode::Stretch, rr);
    const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
    std::printf ("  render took %.0f ms\n", ms);
    CHECK (ok, "render succeeded");
    CHECK (rr.numSamples() == n, "loop length is exactly 8 beats @120");

    bool finite = true; float peak = 0.f;
    for (float v : rr.ch[0]) { if (! std::isfinite (v)) finite = false; peak = std::max (peak, std::abs (v)); }
    CHECK (finite, "no NaN / inf in output");
    CHECK (peak > 0.3f && peak < 1.2f, "output level is sane");

    // detect onsets again in the output: they must now sit on the 1/8 grid
    const auto onsets = detectTransients (rr.ch[0], sr, 0.5, 0, rr.numSamples());
    double worst = 0.0; int on = 0;
    for (double p : onsets)
    {
        const double beat = p / sr * 2.0;                       // 120 bpm -> 2 beats per second
        const double err = std::abs (beat - std::round (beat * 2.0) / 2.0) * 0.5; // seconds
        worst = std::max (worst, err); ++on;
    }
    std::printf ("  %d onsets in output, worst grid error = %.2f ms\n", on, worst * 1000.0);
    CHECK (on >= 14, "all hits survive the warp");
    CHECK (worst < 0.004, "hits are on the grid (< 4 ms)");

    std::printf ("[different host tempo: 140 bpm]\n");
    RenderResult r2;
    CHECK (renderWarp (a, plan, sr, 140.0, StretchMode::Stretch, r2), "render @140 succeeded");
    CHECK (std::abs ((double) r2.numSamples() - beats * 60.0 / 140.0 * sr) < 2.0, "length follows host tempo");

    std::printf ("[other settings]\n");
    RenderResult r3;
    CHECK (renderWarp (a, plan, 48000.0, 120.0, StretchMode::RePitch, r3), "re-pitch mode @48 kHz works");
    CHECK (std::abs ((double) r3.numSamples() - beats * 0.5 * 48000.0) < 2.0, "48 kHz length ok");

    ws.strength = 0.0;
    const auto planNoQuant = computePlan (aw.markers, (double) n, ws);
    RenderResult r4;
    CHECK (renderWarp (a, planNoQuant, sr, 120.0, StretchMode::Stretch, r4), "strength 0% renders");
    // without quantizing, every hit must stay where it was in the source
    // (shifted only by the ~2 ms pre-roll the auto-warp skipped at the start)
    const auto onsets4 = detectTransients (r4.ch[0], sr, 0.5, 0, r4.numSamples());
    double worst4 = 0.0;
    for (double p : onsets4)
    {
        double bestErr = 1e9;
        for (double t : truth) bestErr = std::min (bestErr, std::abs (p / sr - (t - aw.startSample / sr)));
        worst4 = std::max (worst4, bestErr);
    }
    std::printf ("  %zu onsets, worst deviation from source = %.2f ms\n", onsets4.size(), worst4 * 1000.0);
    CHECK (onsets4.size() >= 14 && worst4 < 0.004, "strength 0% keeps hits where they were (< 4 ms)");

    // ---- tonal material: pitch must be preserved and the level must not pump ----
    std::printf ("[sustained 440 Hz tone, stretched 1.25x then squeezed 0.75x]\n");
    {
        AudioData t;
        t.sampleRate = sr;
        t.ch.assign (1, std::vector<float> ((size_t) (2.0 * sr)));
        for (size_t i = 0; i < t.ch[0].size(); ++i)
            t.ch[0][i] = 0.5f * (float) std::sin (2.0 * M_PI * 440.0 * (double) i / sr);

        Marker m; m.sample = 1.0 * sr; m.manual = true; m.manualBeat = 2.5;   // was at beat 2.0
        WarpSettings s;
        s.loopBeats = 4.0; s.strength = 1.0; s.gridBeats = 0.25; s.sourceRate = sr;
        const auto pl = computePlan ({ m }, (double) t.ch[0].size(), s);
        RenderResult tr;
        CHECK (renderWarp (t, pl, sr, 120.0, StretchMode::Stretch, tr), "tone render ok");

        auto freqOf = [&] (size_t a, size_t b)
        {
            int cross = 0;
            for (size_t i = a + 1; i < b; ++i)
                if (tr.ch[0][i - 1] <= 0.f && tr.ch[0][i] > 0.f) ++cross;
            return cross / ((double) (b - a) / sr);
        };
        const double f1 = freqOf ((size_t) (0.15 * sr), (size_t) (1.15 * sr));      // inside the stretched half
        const double f2 = freqOf ((size_t) (1.40 * sr), (size_t) (1.90 * sr));      // inside the squeezed half
        std::printf ("  measured pitch: %.1f Hz / %.1f Hz\n", f1, f2);
        CHECK (std::abs (f1 - 440.0) < 4.0 && std::abs (f2 - 440.0) < 4.0, "pitch preserved (440 Hz +-4)");

        double lo = 1e9, hi = 0.0;
        const size_t w = (size_t) (0.05 * sr);
        for (size_t a = (size_t) (0.1 * sr); a + w < tr.numSamples() - (size_t) (0.1 * sr); a += w)
        {
            if (std::abs ((double) a / sr - 1.25) < 0.06) continue;   // skip the junction itself
            double e = 0.0;
            for (size_t i = a; i < a + w; ++i) e += (double) tr.ch[0][i] * tr.ch[0][i];
            const double rms = std::sqrt (e / (double) w);
            lo = std::min (lo, rms); hi = std::max (hi, rms);
        }
        std::printf ("  rms range over time: %.3f .. %.3f (source 0.354)\n", lo, hi);
        CHECK (lo > 0.30 && hi < 0.40, "level stays steady (no pumping / dropouts)");
    }

    std::printf ("\n%s\n", failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
    return failures == 0 ? 0 : 1;
}
