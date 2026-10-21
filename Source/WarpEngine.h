// AutoWarp - Warp / Quantize engine
// Created by DogSide Productions & Bluethedoggo
//
// This file is intentionally free of any JUCE dependency (std only) so the
// DSP can be unit-tested on its own.
#pragma once

#include <atomic>
#include <cstddef>
#include <vector>

namespace dogside
{

//==============================================================================
struct AudioData
{
    std::vector<std::vector<float>> ch;   // 1 or 2 channels
    double sampleRate = 44100.0;

    size_t numSamples() const { return ch.empty() ? 0 : ch[0].size(); }
};

// A "cut" / warp marker placed on the source audio.
struct Marker
{
    double sample = 0.0;      // position in the source audio (source samples)
    bool   manual = false;    // true when the user dragged the target by hand
    double manualBeat = 0.0;  // target beat (only used when manual == true)
};

enum class StretchMode { Stretch = 0, RePitch = 1 };

// Grid index -> length in beats (quarter note = 1 beat)
inline double gridBeatsFromIndex (int index)
{
    static const double table[] = { 1.0, 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0 };
    if (index < 0) index = 0;
    if (index > 5) index = 5;
    return table[index];
}

struct WarpSettings
{
    double loopBeats   = 16.0;  // how many beats the [start,end] region lasts
    double strength    = 1.0;   // quantize strength 0..1
    double gridBeats   = 0.25;  // grid size in beats
    double swing       = 0.0;   // 0..1
    double startSample = 0.0;   // loop start in the source
    double endSample   = 0.0;   // loop end in the source (<= 0 means "end of file")
    double sourceRate  = 44100.0;
};

struct WarpPoint
{
    double sample = 0.0;      // source position
    double origBeat = 0.0;    // where the audio is originally (in beats)
    double targetBeat = 0.0;  // where it will land after quantizing
    bool   manual = false;
    int    markerIndex = -1;  // index in the marker list, -1 for the start/end anchors
};

struct WarpPlan
{
    std::vector<WarpPoint> points;   // first = start anchor, last = end anchor
    double loopBeats = 0.0;
    bool valid() const { return points.size() >= 2 && loopBeats > 0.0; }
};

WarpPlan computePlan (const std::vector<Marker>& markers,
                      double sourceLength,
                      const WarpSettings& settings);

// Maps a loop position (beats) back to a source position (samples).
double planBeatToSample (const WarpPlan& plan, double beat);

//==============================================================================
struct RenderResult
{
    std::vector<std::vector<float>> ch;
    double sampleRate = 44100.0;
    double bpm = 120.0;
    double loopBeats = 0.0;
    size_t numSamples() const { return ch.empty() ? 0 : ch[0].size(); }
};

// Renders the warped loop. Returns false if cancelled (cancel->load() == true)
// or if the plan is invalid.
bool renderWarp (const AudioData& src,
                 const WarpPlan& plan,
                 double outSampleRate,
                 double bpm,
                 StretchMode mode,
                 RenderResult& result,
                 const std::atomic<bool>* cancel = nullptr);

//==============================================================================
// Analysis
std::vector<float> mixToMono (const AudioData& src);

// Returns onset positions (in samples) inside [from, to).
std::vector<double> detectTransients (const std::vector<float>& mono,
                                      double sampleRate,
                                      double sensitivity,   // 0..1
                                      size_t from, size_t to);

struct AutoWarpResult
{
    std::vector<Marker> markers;
    double startSample = 0.0;
    double endSample = 0.0;
    int    loopBeats = 16;
    double bpm = 120.0;
};

AutoWarpResult autoWarp (const AudioData& src, double sensitivity);

} // namespace dogside
