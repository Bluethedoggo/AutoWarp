// AutoWarp
// Created by DogSide Productions & Bluethedoggo
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

namespace palette
{
    const juce::Colour bg       { 0xff12141a };
    const juce::Colour panel    { 0xff1b1e27 };
    const juce::Colour lane     { 0xff0e1015 };
    const juce::Colour text     { 0xffe8ecf3 };
    const juce::Colour dim      { 0xff8a93a6 };
    const juce::Colour accent   { 0xff3fa9ff };   // "blue"
    const juce::Colour accent2  { 0xffffb347 };   // marker amber
    const juce::Colour manual   { 0xffff5fb0 };
    const juce::Colour loopEdge { 0xff53e0a0 };
}

// Top lane: source audio + editable cuts. Bottom lane: the warped loop on the beat grid.
class WarpView final : public juce::Component,
                       public juce::FileDragAndDropTarget,
                       private juce::Timer
{
public:
    explicit WarpView (AutoWarpAudioProcessor& p);
    ~WarpView() override;

    void zoomFit();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    void timerCallback() override;
    void refreshCache();
    void rebuildPeaks();

    double xToSample (float x) const;
    float  sampleToX (double s) const;
    float  beatToX (double beat) const;
    double xToBeat (float x) const;

    int hitTopMarker (float x) const;      // returns index in the marker list or -1
    int hitBottomMarker (float x) const;
    bool hitLoopEdge (float x, bool start) const;

    AutoWarpAudioProcessor& processor;

    // cached data (message thread only)
    std::shared_ptr<const dogside::AudioData> src;
    std::shared_ptr<const dogside::RenderResult> rendered;
    dogside::WarpPlan plan;
    double loopStart = 0.0, loopEnd = 0.0, gridBeats = 0.25, playBeat = -1.0;

    // view
    double visStart = 0.0, visLen = 1.0;
    const void* lastSrcPtr = nullptr;
    const void* lastRenderPtr = nullptr;

    std::vector<std::pair<float, float>> topPeaks, bottomPeaks;
    double peakStart = -1.0, peakLen = -1.0;
    int peakW = -1;
    bool bottomDirty = true;

    juce::Rectangle<int> topLane, bottomLane;

    enum class Drag { None, Marker, Start, End, Target };
    Drag dragMode = Drag::None;
    int dragIndex = -1;
    bool dragOver = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WarpView)
};
