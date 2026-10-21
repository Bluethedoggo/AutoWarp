// AutoWarp
// Created by DogSide Productions & Bluethedoggo
#include "PluginEditor.h"

namespace
{
    juce::Font fontOf (float h, bool bold = false)
    {
        return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
    }
}

//==============================================================================
DogLookAndFeel::DogLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, palette::bg);
    setColour (juce::TextButton::buttonColourId, palette::panel);
    setColour (juce::TextButton::buttonOnColourId, palette::accent);
    setColour (juce::TextButton::textColourOffId, palette::text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    setColour (juce::ComboBox::backgroundColourId, palette::panel);
    setColour (juce::ComboBox::textColourId, palette::text);
    setColour (juce::ComboBox::outlineColourId, palette::dim.withAlpha (0.4f));
    setColour (juce::ComboBox::arrowColourId, palette::accent);
    setColour (juce::PopupMenu::backgroundColourId, palette::panel);
    setColour (juce::PopupMenu::textColourId, palette::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, palette::accent);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
    setColour (juce::Slider::textBoxTextColourId, palette::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::rotarySliderFillColourId, palette::accent);
    setColour (juce::Slider::rotarySliderOutlineColourId, palette::dim.withAlpha (0.35f));
    setColour (juce::Label::textColourId, palette::text);
    setColour (juce::ToggleButton::textColourId, palette::text);
    setColour (juce::ToggleButton::tickColourId, palette::accent);
    setColour (juce::ToggleButton::tickDisabledColourId, palette::dim);
}

void DogLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                       float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (6.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float arcRadius = radius - 3.0f;
    const float angle = startAngle + sliderPos * (endAngle - startAngle);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (slider.findColour (juce::Slider::rotarySliderOutlineColourId));
    g.strokePath (track, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, angle, true);
    g.setColour (slider.findColour (juce::Slider::rotarySliderFillColourId));
    g.strokePath (value, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour (palette::panel.brighter (0.1f));
    g.fillEllipse (centre.x - radius * 0.55f, centre.y - radius * 0.55f, radius * 1.1f, radius * 1.1f);

    juce::Path tick;
    tick.addRoundedRectangle (-1.5f, -radius * 0.55f, 3.0f, radius * 0.4f, 1.5f);
    g.setColour (palette::text);
    g.fillPath (tick, juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));
}

//==============================================================================
AutoWarpAudioProcessorEditor::AutoWarpAudioProcessorEditor (AutoWarpAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), processor (p), view (p)
{
    setLookAndFeel (&lookAndFeel);
    setSize (920, 640);

    addAndMakeVisible (view);

    for (auto* b : { &loadButton, &autoButton, &previewButton, &exportButton, &clearButton, &fitButton, &aboutButton,
                     &halveButton, &doubleButton })
    {
        styleButton (*b);
        addAndMakeVisible (*b);
    }

    loadButton.onClick    = [this] { chooseAndLoad(); };
    autoButton.onClick    = [this] { processor.autoWarp(); };
    previewButton.onClick = [this] { processor.setPreview (! processor.isPreviewing()); };
    exportButton.onClick  = [this] { chooseAndExport(); };
    clearButton.onClick   = [this] { processor.clearMarkers(); };
    fitButton.onClick     = [this] { view.zoomFit(); };
    aboutButton.onClick   = [this] { showAbout(); };

    halveButton.onClick = [this]
    {
        const int b = (int) std::lround (processor.apvts.getRawParameterValue ("loopBeats")->load());
        processor.setParam ("loopBeats", (float) juce::jmax (1, (b + 1) / 2));
    };
    doubleButton.onClick = [this]
    {
        const int b = (int) std::lround (processor.apvts.getRawParameterValue ("loopBeats")->load());
        processor.setParam ("loopBeats", (float) juce::jmin (512, b * 2));
    };

    infoLabel.setFont (fontOf (13.0f));
    infoLabel.setColour (juce::Label::textColourId, palette::dim);
    addAndMakeVisible (infoLabel);

    setupKnob (strengthKnob,    "QUANTIZE",    "strength",    " %");
    setupKnob (swingKnob,       "SWING",       "swing",       " %");
    setupKnob (sensitivityKnob, "DETECT",      "sensitivity", " %");
    setupKnob (gainKnob,        "GAIN",        "gain",        " dB");
    setupKnob (tempoKnob,       "TEMPO (NO HOST)", "fallbackBpm", "");

    styleSmallLabel (loopBeatsLabel, "Loop length (beats)");
    addAndMakeVisible (loopBeatsLabel);
    loopBeatsSlider.setSliderStyle (juce::Slider::IncDecButtons);
    loopBeatsSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 56, 24);
    loopBeatsSlider.setIncDecButtonsMode (juce::Slider::incDecButtonsNotDraggable);
    addAndMakeVisible (loopBeatsSlider);
    loopBeatsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, "loopBeats", loopBeatsSlider);

    setupCombo (gridBox,     gridLabel,     "Quantize grid", "grid",     gridAttachment);
    setupCombo (stretchBox,  stretchLabel,  "Warp mode",     "stretch",  stretchAttachment);
    setupCombo (playModeBox, playModeLabel, "Playback",      "playMode", playModeAttachment);

    addAndMakeVisible (loopToggle);
    loopAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, "loop", loopToggle);

    startTimerHz (8);
    timerCallback();
}

AutoWarpAudioProcessorEditor::~AutoWarpAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

//==============================================================================
void AutoWarpAudioProcessorEditor::styleButton (juce::TextButton& b)
{
    b.setColour (juce::TextButton::buttonColourId, palette::panel);
}

void AutoWarpAudioProcessorEditor::styleSmallLabel (juce::Label& l, const juce::String& text)
{
    l.setText (text, juce::dontSendNotification);
    l.setFont (fontOf (12.0f, true));
    l.setColour (juce::Label::textColourId, palette::dim);
    l.setJustificationType (juce::Justification::centredLeft);
}

void AutoWarpAudioProcessorEditor::setupKnob (Knob& k, const juce::String& text, const juce::String& paramId, const juce::String& suffix)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
    k.slider.setTextValueSuffix (suffix);
    k.slider.setDoubleClickReturnValue (true, 0.0);
    addAndMakeVisible (k.slider);

    styleSmallLabel (k.label, text);
    k.label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (k.label);

    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, paramId, k.slider);
}

void AutoWarpAudioProcessorEditor::setupCombo (juce::ComboBox& box, juce::Label& label, const juce::String& text,
                                               const juce::String& paramId,
                                               std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>& attachment)
{
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (paramId)))
        box.addItemList (choice->choices, 1);

    addAndMakeVisible (box);
    styleSmallLabel (label, text);
    addAndMakeVisible (label);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.apvts, paramId, box);
}

//==============================================================================
void AutoWarpAudioProcessorEditor::chooseAndLoad()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a sample, loop or song", juce::File(),
                                                   "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f.existsAsFile() && ! processor.loadFile (f))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "AutoWarp",
                                                    "Could not read that audio file.");
    });
}

void AutoWarpAudioProcessorEditor::chooseAndExport()
{
    if (processor.getRendered() == nullptr)
        return;

    const auto start = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                           .getChildFile (processor.getSampleName() + "_warped.wav");

    chooser = std::make_unique<juce::FileChooser> ("Export the warped loop", start, "*.wav");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f == juce::File()) return;
        if (! processor.exportWav (f.withFileExtension ("wav")))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "AutoWarp",
                                                    "Could not write the file.");
    });
}

void AutoWarpAudioProcessorEditor::showAbout()
{
    juce::AlertWindow::showMessageBoxAsync (
        juce::MessageBoxIconType::InfoIcon, "About AutoWarp",
        juce::String ("AutoWarp\n\n")
            + "Automatic warp & quantize for samples, loops and songs.\n\n"
            + "Created by " + AUTOWARP_CREDITS_COMPANY + "\n"
            + "and " + AUTOWARP_CREDITS_AUTHOR + "\n\n"
            + "(c) DogSide Productions. All rights reserved.");
}

//==============================================================================
void AutoWarpAudioProcessorEditor::timerCallback()
{
    previewButton.setButtonText (processor.isPreviewing() ? "STOP" : "PREVIEW");
    previewButton.setToggleState (processor.isPreviewing(), juce::dontSendNotification);

    const auto src = processor.getSource();
    if (src == nullptr)
    {
        infoLabel.setText ("No sample loaded", juce::dontSendNotification);
        return;
    }

    const double dur = (processor.getLoopEnd() - processor.getLoopStart()) / src->sampleRate;
    const int beats = (int) std::lround (processor.apvts.getRawParameterValue ("loopBeats")->load());
    const double origBpm = dur > 0.0 ? (double) beats * 60.0 / dur : 0.0;

    infoLabel.setText (processor.getSampleName() + "   |   " + juce::String (beats) + " beats ("
                           + juce::String ((double) beats / 4.0, 2) + " bars)   |   original tempo "
                           + juce::String (origBpm, 1) + " BPM   |   playing at "
                           + juce::String (processor.getCurrentBpm(), 1) + " BPM   |   "
                           + juce::String ((int) processor.getMarkers().size()) + " cuts",
                       juce::dontSendNotification);
}

//==============================================================================
void AutoWarpAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (palette::bg);

    // title
    g.setColour (palette::accent);
    g.setFont (fontOf (28.0f, true));
    g.drawText ("AUTOWARP", 18, 10, 230, 30, juce::Justification::centredLeft);

    g.setColour (palette::dim);
    g.setFont (fontOf (12.0f));
    g.drawText (juce::String ("by ") + AUTOWARP_CREDITS_COMPANY + "  -  " + AUTOWARP_CREDITS_AUTHOR,
                18, 38, 300, 16, juce::Justification::centredLeft);

    // controls panel
    g.setColour (palette::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat().withTop (432.0f).withTrimmedBottom (34.0f).reduced (14.0f, 0.0f), 8.0f);

    // footer / credits
    g.setColour (palette::dim);
    g.setFont (fontOf (12.0f));
    g.drawText (juce::String ("AutoWarp  |  Created by ") + AUTOWARP_CREDITS_COMPANY + "  &  " + AUTOWARP_CREDITS_AUTHOR
                    + "  |  (c) DogSide Productions",
                getLocalBounds().removeFromBottom (30), juce::Justification::centred);
}

void AutoWarpAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced (14, 10);

    // header
    auto header = r.removeFromTop (46);
    const int bw = 88, gap = 6;
    for (auto* b : { &aboutButton, &fitButton, &clearButton, &exportButton, &previewButton, &autoButton, &loadButton })
    {
        b->setBounds (header.removeFromRight (bw + (b == &autoButton ? 14 : 0)).withSizeKeepingCentre (bw + (b == &autoButton ? 14 : 0), 30));
        header.removeFromRight (gap);
    }

    infoLabel.setBounds (r.removeFromTop (24));

    // waveform view
    view.setBounds (r.removeFromTop (340));

    // controls
    auto panel = getLocalBounds().withTop (432).withTrimmedBottom (34).reduced (26, 10);

    auto left = panel.removeFromLeft (400);
    auto row1 = left.removeFromTop (34);
    loopBeatsLabel.setBounds (row1.removeFromLeft (130));
    loopBeatsSlider.setBounds (row1.removeFromLeft (120).reduced (0, 4));
    row1.removeFromLeft (8);
    halveButton.setBounds (row1.removeFromLeft (40).reduced (0, 4));
    row1.removeFromLeft (6);
    doubleButton.setBounds (row1.removeFromLeft (40).reduced (0, 4));

    left.removeFromTop (6);
    auto row2 = left.removeFromTop (34);
    gridLabel.setBounds (row2.removeFromLeft (130));
    gridBox.setBounds (row2.removeFromLeft (150).reduced (0, 4));

    left.removeFromTop (6);
    auto row3 = left.removeFromTop (34);
    stretchLabel.setBounds (row3.removeFromLeft (130));
    stretchBox.setBounds (row3.removeFromLeft (180).reduced (0, 4));

    left.removeFromTop (6);
    auto row4 = left.removeFromTop (34);
    playModeLabel.setBounds (row4.removeFromLeft (130));
    playModeBox.setBounds (row4.removeFromLeft (150).reduced (0, 4));
    row4.removeFromLeft (10);
    loopToggle.setBounds (row4.removeFromLeft (80));

    panel.removeFromLeft (20);

    const int kw = panel.getWidth() / 5;
    for (auto* k : { &strengthKnob, &swingKnob, &sensitivityKnob, &gainKnob, &tempoKnob })
    {
        auto cell = panel.removeFromLeft (kw);
        k->label.setBounds (cell.removeFromTop (18));
        k->slider.setBounds (cell);
    }
}
