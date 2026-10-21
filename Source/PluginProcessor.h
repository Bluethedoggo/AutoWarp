// AutoWarp
// Created by DogSide Productions & Bluethedoggo
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <deque>
#include <memory>

#include "WarpEngine.h"

#define AUTOWARP_CREDITS_COMPANY  "DogSide Productions"
#define AUTOWARP_CREDITS_AUTHOR   "Bluethedoggo"

class AutoWarpAudioProcessor final : public juce::AudioProcessor,
                                     private juce::AudioProcessorValueTreeState::Listener,
                                     private juce::Thread
{
public:
    AutoWarpAudioProcessor();
    ~AutoWarpAudioProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "AutoWarp"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    // Sample handling (called from the UI / message thread)
    bool loadFile (const juce::File& file);
    void autoWarp();                       // detect cuts + loop length
    bool exportWav (const juce::File& file);

    std::shared_ptr<const dogside::AudioData> getSource() const;
    juce::String getSampleName() const;

    // Markers ("cuts")
    std::vector<dogside::Marker> getMarkers() const;
    void addMarker (double sample);
    void removeMarker (int index);
    int  moveMarker (int index, double newSample);          // returns the new index
    void setMarkerManual (int index, bool manual, double beat);
    void clearMarkers();

    double getLoopStart() const;
    double getLoopEnd() const;               // always resolved (never 0)
    void   setLoopRange (double start, double end);

    // Snapshot of the current warp plan (for drawing and hit-testing)
    dogside::WarpPlan getPlan() const;
    double getGridBeats() const;

    // Latest rendered loop (for drawing / export)
    std::shared_ptr<const dogside::RenderResult> getRendered() const;

    void setPreview (bool shouldPlay) { previewCmd.store (shouldPlay ? 1 : 2); }
    bool isPreviewing() const { return previewActive.load(); }

    // Playhead position inside the loop, in beats; < 0 when silent
    double getPlayheadBeat() const { return playheadBeat.load(); }
    double getCurrentBpm() const { return hostBpmAtomic.load(); }

    void setParam (const juce::String& id, float plainValue);

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void parameterChanged (const juce::String& parameterID, float newValue) override;

    // render thread
    void run() override;
    void requestRender();
    void doRender();

    struct Snapshot
    {
        std::shared_ptr<const dogside::AudioData> src;
        std::vector<dogside::Marker> markers;
        dogside::WarpSettings settings;
        dogside::StretchMode mode = dogside::StretchMode::Stretch;
    };
    Snapshot takeSnapshot() const;

    void setSource (std::shared_ptr<const dogside::AudioData> data, const juce::String& name, const juce::String& path);
    void renderSpan (juce::AudioBuffer<float>& buffer, int start, int num, double bpm, double sr,
                     bool hostPlaying, bool hasPpq, double ppq, bool loop, bool syncMode, float gain);

    //==========================================================================
    juce::AudioFormatManager formats;

    mutable juce::CriticalSection dataLock;
    std::shared_ptr<const dogside::AudioData> source;
    std::vector<dogside::Marker> markers;
    double startSample = 0.0, endSample = 0.0;
    juce::String sampleName, samplePath;

    // render -> audio hand-over
    mutable juce::SpinLock publishLock;
    std::shared_ptr<const dogside::RenderResult> published;
    std::deque<std::shared_ptr<const dogside::RenderResult>> history;   // keeps old results alive off the audio thread
    std::atomic<int> publishedVersion { 0 };
    std::atomic<bool> renderRequested { false };
    std::atomic<double> hostBpmAtomic { 120.0 };
    std::atomic<double> currentSampleRate { 44100.0 };

    // audio-thread state
    std::shared_ptr<const dogside::RenderResult> audioRender;
    int audioVersion = -1;
    double lastRequestedBpm = 0.0;
    bool voiceActive = false;
    double voiceBeat = 0.0;
    float voiceGain = 1.0f;
    int fadeRemaining = 0;
    double expectedLoopPos = -1.0;
    double lastLoopPos = -1.0;
    std::atomic<int> previewCmd { 0 };
    std::atomic<bool> previewActive { false };
    bool previewVoice = false;
    std::atomic<double> playheadBeat { -1.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutoWarpAudioProcessor)
};
