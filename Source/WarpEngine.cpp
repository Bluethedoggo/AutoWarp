// AutoWarp - Warp / Quantize engine
// Created by DogSide Productions & Bluethedoggo
#include "WarpEngine.h"

#include <algorithm>
#include <cmath>

namespace dogside
{
namespace
{
    constexpr size_t kPad = 16384;     // zero padding around every source buffer
    constexpr double kPi  = 3.14159265358979323846;

    inline double clampd (double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

    // 4-point Catmull-Rom interpolation. `p` points at sample 0 of a padded buffer.
    inline float cubicAt (const float* p, double pos)
    {
        const long   i = (long) std::floor (pos);
        const double f = pos - (double) i;
        const double y0 = p[i - 1], y1 = p[i], y2 = p[i + 1], y3 = p[i + 2];
        const double a = -0.5 * y0 + 1.5 * y1 - 1.5 * y2 + 0.5 * y3;
        const double b = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3;
        const double c = -0.5 * y0 + 0.5 * y2;
        return (float) (((a * f + b) * f + c) * f + y1);
    }

    std::vector<float> makePadded (const std::vector<float>& in)
    {
        std::vector<float> out (kPad + in.size() + kPad, 0.0f);
        std::copy (in.begin(), in.end(), out.begin() + (long) kPad);
        return out;
    }
}

//==============================================================================
// Plan (marker positions + quantize)
//==============================================================================
WarpPlan computePlan (const std::vector<Marker>& markers, double sourceLength, const WarpSettings& s)
{
    WarpPlan plan;

    const double start = clampd (s.startSample, 0.0, sourceLength);
    const double end   = (s.endSample > 0.0) ? clampd (s.endSample, 0.0, sourceLength) : sourceLength;

    if (end - start < 64.0 || s.loopBeats <= 0.0)
        return plan;

    const double span = end - start;
    plan.loopBeats = s.loopBeats;

    auto toBeat = [&] (double smp) { return (smp - start) / span * s.loopBeats; };

    // interior markers, sorted by position
    std::vector<int> idx;
    for (int i = 0; i < (int) markers.size(); ++i)
        if (markers[(size_t) i].sample > start + 8.0 && markers[(size_t) i].sample < end - 8.0)
            idx.push_back (i);

    std::stable_sort (idx.begin(), idx.end(), [&] (int a, int b)
    {
        return markers[(size_t) a].sample < markers[(size_t) b].sample;
    });

    WarpPoint first;
    first.sample = start; first.origBeat = 0.0; first.targetBeat = 0.0;
    plan.points.push_back (first);

    const double grid = s.gridBeats > 1.0e-6 ? s.gridBeats : 0.25;

    for (int i : idx)
    {
        const Marker& m = markers[(size_t) i];
        WarpPoint p;
        p.sample = m.sample;
        p.origBeat = toBeat (m.sample);
        p.markerIndex = i;
        p.manual = m.manual;

        if (m.manual)
        {
            p.targetBeat = m.manualBeat;
        }
        else
        {
            const long slot = std::lround (p.origBeat / grid);
            double q = (double) slot * grid;
            if ((slot & 1L) != 0)
                q += s.swing * grid / 3.0;   // swing delays every second grid slot
            p.targetBeat = p.origBeat + (q - p.origBeat) * s.strength;
        }
        plan.points.push_back (p);
    }

    WarpPoint last;
    last.sample = end; last.origBeat = s.loopBeats; last.targetBeat = s.loopBeats;
    plan.points.push_back (last);

    // ---- keep the targets ordered ------------------------------------------
    const size_t n = plan.points.size();
    const double minSeg = 0.015 * (s.sourceRate > 0.0 ? s.sourceRate : 44100.0);
    const double minGap = std::min (grid * 0.25, s.loopBeats / (4.0 * (double) n));

    // gap[i] = minimum distance between point i-1 and point i
    std::vector<double> gap (n, 0.0);
    for (size_t i = 2; i < n; ++i)   // first segment may collapse (leading silence)
        gap[i] = (plan.points[i].sample - plan.points[i - 1].sample) >= minSeg ? minGap : 0.0;

    for (size_t i = 1; i + 1 < n; ++i)
        plan.points[i].targetBeat = clampd (plan.points[i].targetBeat, 0.0, s.loopBeats);

    for (size_t i = 1; i + 1 < n; ++i)
        plan.points[i].targetBeat = std::max (plan.points[i].targetBeat, plan.points[i - 1].targetBeat + gap[i]);

    for (size_t i = n - 2; i >= 1; --i)
    {
        plan.points[i].targetBeat = std::min (plan.points[i].targetBeat, plan.points[i + 1].targetBeat - gap[i + 1]);
        if (i == 1) break;
    }

    for (size_t i = 1; i < n; ++i)   // final safety net: never go backwards
        plan.points[i].targetBeat = std::max (plan.points[i].targetBeat, plan.points[i - 1].targetBeat);

    return plan;
}

double planBeatToSample (const WarpPlan& plan, double beat)
{
    if (! plan.valid())
        return 0.0;

    const auto& p = plan.points;
    if (beat <= p.front().targetBeat) return p.front().sample;
    if (beat >= p.back().targetBeat)  return p.back().sample;

    for (size_t i = 0; i + 1 < p.size(); ++i)
    {
        const double t0 = p[i].targetBeat, t1 = p[i + 1].targetBeat;
        if (beat >= t0 && beat < t1 && t1 > t0)
        {
            const double f = (beat - t0) / (t1 - t0);
            return p[i].sample + f * (p[i + 1].sample - p[i].sample);
        }
    }
    return p.back().sample;
}

//==============================================================================
// Time stretching (WSOLA, driven by a piecewise time map)
//==============================================================================
namespace
{
    struct StretchContext
    {
        int    F = 1024;       // grain length
        int    Hs = 256;       // synthesis hop
        int    tolMax = 260;   // max search offset (samples)
        int    corrLen = 512;  // correlation length
        double rate = 44100.0;
        std::vector<float> window;
    };

    StretchContext makeContext (double sampleRate)
    {
        StretchContext c;
        c.rate = sampleRate;
        int F = 256;
        while (F < 0.021 * sampleRate && F < 4096) F *= 2;
        c.F = F;
        c.Hs = F / 4;
        c.tolMax = std::max (16, (int) std::lround (0.006 * sampleRate));
        c.corrLen = F / 2;
        c.window.resize ((size_t) F);
        for (int i = 0; i < F; ++i)
            c.window[(size_t) i] = (float) (0.5 * (1.0 - std::cos (2.0 * kPi * (double) i / (double) F)));
        return c;
    }

    // Stretches source range [s0, s1) to exactly `outLen` samples and writes it
    // into out[ch][outOffset ...]. The first ~10 ms (the attack) is kept at 1:1
    // speed so transients stay sharp.
    void wsolaSegment (const StretchContext& c,
                       const std::vector<std::vector<float>>& padCh,
                       const std::vector<float>& padMono,
                       double s0, double s1, long outLen,
                       std::vector<std::vector<float>>& scratch,
                       std::vector<std::vector<float>>& out, long outOffset)
    {
        const int F = c.F, Hs = c.Hs;
        const double srcLen = s1 - s0;
        const double attack = std::min (0.010 * c.rate, 0.3 * std::min ((double) outLen, srcLen));

        const float* mono = padMono.data() + (long) kPad;
        const long accLen = outLen + 2L * F;
        for (auto& a : scratch) a.assign ((size_t) accLen, 0.0f);

        auto mapT = [&] (double t) -> double
        {
            if (t <= attack)          return s0 + t;
            if (t >= (double) outLen) return s1 + (t - (double) outLen);
            return s0 + attack + (t - attack) * (srcLen - attack) / ((double) outLen - attack);
        };

        long prevStart = 0;
        bool havePrev = false;

        for (long o = -3L * Hs; o < outLen; o += Hs)
        {
            const double tc = (double) o + 0.5 * F;
            const long b0 = std::lround (mapT (tc) - 0.5 * F);

            // search tolerance fades to zero near the attack and the segment end
            const double d = std::min ((double) o - attack, (double) (outLen - (o + F)));
            const int tol = (int) ((double) c.tolMax * clampd (d / (double) F, 0.0, 1.0));

            long best = b0;
            if (havePrev && tol > 0)
            {
                const float* ref = mono + (prevStart + Hs);
                double bestScore = -1.0e30;

                auto evalAt = [&] (long cand) -> double
                {
                    const float* cp = mono + cand;
                    double dot = 0.0, en = 1.0e-9;
                    for (int k = 0; k < c.corrLen; k += 2)
                    {
                        dot += (double) ref[k] * (double) cp[k];
                        en  += (double) cp[k] * (double) cp[k];
                    }
                    return dot / std::sqrt (en);
                };

                for (long dl = -tol; dl <= tol; dl += 4)
                {
                    const double sc = evalAt (b0 + dl);
                    if (sc > bestScore) { bestScore = sc; best = b0 + dl; }
                }
                const long centre = best;
                for (long dl = -3; dl <= 3; ++dl)
                {
                    const long cand = centre + dl;
                    if (cand < b0 - tol || cand > b0 + tol) continue;
                    const double sc = evalAt (cand);
                    if (sc > bestScore) { bestScore = sc; best = cand; }
                }
            }

            const long base = o + F;
            for (size_t ch = 0; ch < scratch.size(); ++ch)
            {
                const float* sp = padCh[ch].data() + (long) kPad + best;
                float* acc = scratch[ch].data() + base;
                for (int n = 0; n < F; ++n)
                    acc[n] += c.window[(size_t) n] * sp[n];
            }

            prevStart = best;
            havePrev = true;
        }

        for (size_t ch = 0; ch < out.size(); ++ch)
            for (long j = 0; j < outLen; ++j)
                out[ch][(size_t) (outOffset + j)] = scratch[ch][(size_t) (F + j)] * 0.5f;   // Hann @ 75% overlap sums to 2
    }
}

//==============================================================================
bool renderWarp (const AudioData& src, const WarpPlan& plan, double outRate, double bpm,
                 StretchMode mode, RenderResult& result, const std::atomic<bool>* cancel)
{
    if (! plan.valid() || src.ch.empty() || src.numSamples() < 64 || outRate <= 0.0 || bpm <= 0.0)
        return false;

    const size_t nch = std::min<size_t> (2, src.ch.size());
    const double ratio = outRate / src.sampleRate;

    // ---- 1. bring the source to the output sample rate, zero padded ---------
    std::vector<std::vector<float>> padCh (nch);
    size_t n = src.numSamples();

    if (std::abs (ratio - 1.0) < 1.0e-9)
    {
        for (size_t c = 0; c < nch; ++c) padCh[c] = makePadded (src.ch[c]);
    }
    else
    {
        n = (size_t) std::llround ((double) n * ratio);
        for (size_t c = 0; c < nch; ++c)
        {
            const std::vector<float> in = makePadded (src.ch[c]);
            std::vector<float> out (kPad + n + kPad, 0.0f);
            const float* ip = in.data() + (long) kPad;
            for (size_t i = 0; i < n; ++i)
                out[kPad + i] = cubicAt (ip, (double) i / ratio);
            padCh[c] = std::move (out);
            if (cancel != nullptr && cancel->load()) return false;
        }
    }

    std::vector<float> padMono (kPad + n + kPad, 0.0f);
    for (size_t i = 0; i < padMono.size(); ++i)
    {
        float s = 0.0f;
        for (size_t c = 0; c < nch; ++c) s += padCh[c][i];
        padMono[i] = s / (float) nch;
    }

    // ---- 2. render every segment between two warp points --------------------
    const double spb = 60.0 / bpm * outRate;                 // samples per beat
    const long totalOut = std::max (1L, std::lround (plan.loopBeats * spb));

    result.ch.assign (nch, std::vector<float> ((size_t) totalOut, 0.0f));
    result.sampleRate = outRate;
    result.bpm = bpm;
    result.loopBeats = plan.loopBeats;

    const StretchContext ctx = makeContext (outRate);
    std::vector<std::vector<float>> scratch (nch);

    for (size_t i = 0; i + 1 < plan.points.size(); ++i)
    {
        if (cancel != nullptr && cancel->load()) return false;

        const long o0 = std::lround (plan.points[i].targetBeat * spb);
        const long o1 = std::min (totalOut, std::lround (plan.points[i + 1].targetBeat * spb));
        const long outLen = o1 - o0;
        if (outLen < 2) continue;

        const double s0 = plan.points[i].sample * ratio;
        const double s1 = plan.points[i + 1].sample * ratio;
        const double srcLen = s1 - s0;
        if (srcLen < 2.0) continue;

        const bool simple = (mode == StretchMode::RePitch) || std::abs ((double) outLen - srcLen) < 0.5;

        if (simple)
        {
            for (size_t c = 0; c < nch; ++c)
            {
                const float* p = padCh[c].data() + (long) kPad;
                for (long j = 0; j < outLen; ++j)
                    result.ch[c][(size_t) (o0 + j)] = cubicAt (p, s0 + (double) j * srcLen / (double) outLen);
            }
        }
        else
        {
            wsolaSegment (ctx, padCh, padMono, s0, s1, outLen, scratch, result.ch, o0);
        }
    }

    return true;
}

//==============================================================================
// Analysis
//==============================================================================
std::vector<float> mixToMono (const AudioData& src)
{
    std::vector<float> mono (src.numSamples(), 0.0f);
    if (src.ch.empty()) return mono;
    for (const auto& c : src.ch)
        for (size_t i = 0; i < mono.size() && i < c.size(); ++i)
            mono[i] += c[i];
    const float g = 1.0f / (float) src.ch.size();
    for (auto& v : mono) v *= g;
    return mono;
}

namespace
{
    // Energy envelope (dB) of the pre-emphasised signal.
    std::vector<double> computeEnvDb (const std::vector<float>& x, size_t from, size_t to, size_t hop, size_t win)
    {
        const size_t nFrames = to > from ? (to - from) / hop : 0;
        std::vector<double> env (nFrames, -100.0);
        for (size_t f = 0; f < nFrames; ++f)
        {
            const size_t s = from + f * hop;
            const size_t e = std::min (s + win, to);
            double sum = 0.0;
            for (size_t i = std::max<size_t> (s, 1); i < e; ++i)
            {
                const double d = (double) x[i] - 0.97 * (double) x[i - 1];
                sum += d * d;
            }
            const double rms = std::sqrt (sum / (double) std::max<size_t> (1, e - s));
            env[f] = std::max (-100.0, 20.0 * std::log10 (rms + 1.0e-7));
        }
        return env;
    }
}

std::vector<double> detectTransients (const std::vector<float>& x, double sr, double sensitivity,
                                      size_t from, size_t to)
{
    std::vector<double> result;
    if (to > x.size()) to = x.size();
    if (to <= from + 2048 || sr <= 0.0) return result;

    sensitivity = clampd (sensitivity, 0.0, 1.0);

    const size_t hop = std::max<size_t> (32, (size_t) (sr * 0.004));
    const size_t win = hop * 2;
    const std::vector<double> env = computeEnvDb (x, from, to, hop, win);
    const size_t nFrames = env.size();
    if (nFrames < 8) return result;

    const double thrDb = 9.0 - 6.0 * sensitivity;                       // needed level rise (dB)
    const double floorDb = -52.0;                                       // ignore very quiet material
    const double minDist = sr * (0.030 + (1.0 - sensitivity) * 0.050);  // min distance between hits

    double lastPos = -1.0e12;
    bool armed = true;

    for (size_t f = 2; f < nFrames; ++f)
    {
        const double flux = (env[f] > floorDb) ? env[f] - env[f - 2] : 0.0;
        const double framePos = (double) (from + f * hop);

        if (flux >= thrDb && armed && framePos - lastPos >= minDist)
        {
            // ---- refine to sample accuracy ----
            const size_t a = from + (f - 1) * hop;
            const size_t b = std::min (to, a + win + 2 * hop);

            double peak = 0.0;
            for (size_t i = std::max<size_t> (a, 1); i < b; ++i)
                peak = std::max (peak, std::abs ((double) x[i] - 0.97 * (double) x[i - 1]));

            size_t pos = a;
            if (peak > 1.0e-6)
            {
                double smooth = 0.0;
                for (size_t i = std::max<size_t> (a, 8); i < b; ++i)
                {
                    smooth = 0.0;
                    for (size_t k = 0; k < 8; ++k)
                        smooth += std::abs ((double) x[i - k] - 0.97 * (double) x[i - k - 1]);
                    smooth /= 8.0;
                    if (smooth >= 0.30 * peak) { pos = i; break; }
                }
            }

            pos = pos > 24 ? pos - 24 : 0;   // small pre-roll
            if (pos < from) pos = from;

            // snap back to a zero crossing (avoids clicks when cutting)
            for (size_t k = 0; k < 96 && pos > from + 1; ++k, --pos)
                if ((x[pos] <= 0.0f && x[pos - 1] > 0.0f) || (x[pos] >= 0.0f && x[pos - 1] < 0.0f))
                    break;

            result.push_back ((double) pos);
            lastPos = (double) pos;
            armed = false;
        }
        else if (flux < thrDb * 0.4)
        {
            armed = true;
        }
    }
    return result;
}

namespace
{
    // Picks how many beats the loop lasts, by checking how well a regular beat
    // grid (anchored at the loop start) lines up with the onsets.
    int estimateLoopBeats (const std::vector<float>& x, double sr, size_t from, size_t to)
    {
        const double dur = (double) (to - from) / sr;
        const int fallback = std::max (1, (int) std::lround (dur * 2.0));   // = 120 bpm
        if (dur < 0.2) return 1;

        const size_t hop = std::max<size_t> (32, (size_t) (sr * 0.005));
        const std::vector<double> env = computeEnvDb (x, from, to, hop, hop * 2);
        const size_t nF = env.size();
        if (nF < 8) return fallback;

        std::vector<double> flux (nF, 0.0);
        double mean = 0.0;
        for (size_t f = 1; f < nF; ++f)
        {
            flux[f] = clampd (env[f] - env[f - 1], 0.0, 24.0);
            mean += flux[f];
        }
        mean /= (double) nF;

        double bestScore = -1.0;
        int bestN = fallback;
        const int maxN = 4096;

        for (int N = 1; N <= maxN; ++N)
        {
            const double bpm = (double) N * 60.0 / dur;
            if (bpm < 60.0) continue;
            if (bpm > 200.0) break;

            const double period = (double) nF / (double) N;
            double sum = 0.0;
            for (int k = 0; k < N; ++k)
            {
                const long c = std::lround ((double) k * period);
                double m = 0.0;
                for (long j = c - 1; j <= c + 2; ++j)
                    if (j >= 0 && j < (long) nF) m = std::max (m, flux[(size_t) j]);
                sum += m;
            }
            const double hit = sum / (double) N;
            const double oct = std::log2 (bpm / 120.0) / 0.6;
            const double prior = std::exp (-0.5 * oct * oct);
            const double score = hit / (mean + 1.0e-6) * prior;
            if (score > bestScore) { bestScore = score; bestN = N; }
        }
        return bestN;
    }
}

AutoWarpResult autoWarp (const AudioData& src, double sensitivity)
{
    AutoWarpResult r;
    const size_t n = src.numSamples();
    r.startSample = 0.0;
    r.endSample = (double) n;
    if (n < 2048) return r;

    const std::vector<float> mono = mixToMono (src);

    // Only skip a *short* silent pre-roll (< 100 ms) so the first hit becomes
    // beat 1. Longer silences are musical (a rest) and are kept. The end of
    // the file is never trimmed: loops normally end in a decayed tail.
    size_t first = 0;
    while (first < n && std::abs (mono[first]) < 0.01f) ++first;

    size_t start = 0;
    const size_t preRoll = (size_t) (src.sampleRate * 0.002);
    if (first < n && first > preRoll && (double) first < 0.100 * src.sampleRate)
        start = first - preRoll;
    const size_t end = n;

    r.startSample = (double) start;
    r.endSample = (double) end;

    const auto onsets = detectTransients (mono, src.sampleRate, sensitivity, start, end);
    const double edge = src.sampleRate * 0.012;
    for (double p : onsets)
    {
        if (p - (double) start < edge) continue;     // first hit = loop start
        if ((double) end - p < edge * 1.5) continue;
        Marker m;
        m.sample = p;
        r.markers.push_back (m);
    }

    r.loopBeats = estimateLoopBeats (mono, src.sampleRate, start, end);
    r.bpm = (double) r.loopBeats * 60.0 / ((double) (end - start) / src.sampleRate);
    return r;
}

} // namespace dogside
