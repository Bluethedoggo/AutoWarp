// AutoWarp
// Created by DogSide Productions & Bluethedoggo
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "WarpView.h"

class DogLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    DogLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
};

class AutoWarpAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                           private juce::Timer
{
public:
    explicit AutoWarpAudioProcessorEditor (AutoWarpAudioProcessor&);
    ~AutoWarpAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void showAbout();
    void chooseAndLoad();
    void chooseAndExport();

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    void setupKnob (Knob& k, const juce::String& text, const juce::String& paramId, const juce::String& suffix);
    void setupCombo (juce::ComboBox& box, juce::Label& label, const juce::String& text, const juce::String& paramId,
                     std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>& attachment);
    void styleButton (juce::TextButton& b);
    void styleSmallLabel (juce::Label& l, const juce::String& text);

    AutoWarpAudioProcessor& processor;
    DogLookAndFeel lookAndFeel;

    WarpView view;

    juce::TextButton loadButton { "LOAD" }, autoButton { "AUTO-WARP" }, previewButton { "PREVIEW" },
                     exportButton { "EXPORT WAV" }, clearButton { "CLEAR CUTS" }, fitButton { "FIT" },
                     aboutButton { "ABOUT" }, halveButton { "/2" }, doubleButton { "x2" };

    juce::Label infoLabel;

    Knob strengthKnob, swingKnob, sensitivityKnob, gainKnob, tempoKnob;

    juce::Slider loopBeatsSlider;
    juce::Label loopBeatsLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> loopBeatsAttachment;

    juce::ComboBox gridBox, stretchBox, playModeBox;
    juce::Label gridLabel, stretchLabel, playModeLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> gridAttachment, stretchAttachment, playModeAttachment;

    juce::ToggleButton loopToggle { "Loop" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> loopAttachment;

    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutoWarpAudioProcessorEditor)
};
