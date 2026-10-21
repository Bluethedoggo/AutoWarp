// AutoWarp
// Created by DogSide Productions & Bluethedoggo
#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr int kFadeSamples = 64;
    constexpr juce::int32 kAudioMagic = 0x31565741; // "AWV1"
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout AutoWarpAudioProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterInt> (ParameterID { "loopBeats", 1 }, "Loop Beats", 1, 512, 16));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "strength", 1 }, "Quantize Strength",
                    NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
                    AudioParameterFloatAttributes().withLabel ("%")));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "grid", 1 }, "Quantize Grid",
                    StringArray { "1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T" }, 2));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "swing", 1 }, "Swing",
                    NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f,
                    AudioParameterFloatAttributes().withLabel ("%")));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "sensitivity", 1 }, "Detect Sensitivity",
                    NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f,
                    AudioParameterFloatAttributes().withLabel ("%")));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "stretch", 1 }, "Warp Mode",
                    StringArray { "Beats (keeps pitch)", "Re-Pitch" }, 0));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "playMode", 1 }, "Playback",
                    StringArray { "Host Sync", "MIDI Trigger" }, 0));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "loop", 1 }, "Loop", true));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "gain", 1 }, "Gain",
                    NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
                    AudioParameterFloatAttributes().withLabel ("dB")));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "fallbackBpm", 1 }, "Tempo (no host)",
                    NormalisableRange<float> (20.0f, 300.0f, 0.01f), 120.0f,
                    AudioParameterFloatAttributes().withLabel ("BPM")));

    return layout;
}

//==============================================================================
AutoWarpAudioProcessor::AutoWarpAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      juce::Thread ("AutoWarp renderer"),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    formats.registerBasicFormats();

    for (auto id : { "loopBeats", "strength", "grid", "swing", "stretch", "fallbackBpm" })
        apvts.addParameterListener (id, this);

    startThread();
}

AutoWarpAudioProcessor::~AutoWarpAudioProcessor()
{
    for (auto id : { "loopBeats", "strength", "grid", "swing", "stretch", "fallbackBpm" })
        apvts.removeParameterListener (id, this);

    stopThread (4000);
}

bool AutoWarpAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

juce::AudioProcessorEditor* AutoWarpAudioProcessor::createEditor()
{
    return new AutoWarpAudioProcessorEditor (*this);
}

void AutoWarpAudioProcessor::setParam (const juce::String& id, float plainValue)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
        p->endChangeGesture();
    }
}

void AutoWarpAudioProcessor::parameterChanged (const juce::String& parameterID, float newValue)
{
    if (parameterID == "fallbackBpm")
    {
        // only matters when the host does not tell us its tempo; the audio thread decides
        juce::ignoreUnused (newValue);
        return;
    }
    requestRender();
}

//==============================================================================
// Render thread
//==============================================================================
void AutoWarpAudioProcessor::requestRender()
{
    renderRequested.store (true);
    notify();
}

void AutoWarpAudioProcessor::run()
{
    while (! threadShouldExit())
    {
        wait (-1);

        while (renderRequested.exchange (false) && ! threadShouldExit())
            doRender();
    }
}

AutoWarpAudioProcessor::Snapshot AutoWarpAudioProcessor::takeSnapshot() const
{
    Snapshot s;
    {
        const juce::ScopedLock sl (dataLock);
        s.src = source;
        s.markers = markers;
        s.settings.startSample = startSample;
        s.settings.endSample = endSample;
    }

    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    s.settings.loopBeats  = (double) juce::jmax (1, (int) std::lround (get ("loopBeats")));
    s.settings.strength   = (double) get ("strength") / 100.0;
    s.settings.gridBeats  = dogside::gridBeatsFromIndex ((int) std::lround (get ("grid")));
    s.settings.swing      = (double) get ("swing") / 100.0;
    s.settings.sourceRate = s.src ? s.src->sampleRate : 44100.0;
    s.mode = ((int) std::lround (get ("stretch")) == 1) ? dogside::StretchMode::RePitch
                                                         : dogside::StretchMode::Stretch;
    return s;
}

void AutoWarpAudioProcessor::doRender()
{
    const Snapshot snap = takeSnapshot();

    std::shared_ptr<dogside::RenderResult> result;

    if (snap.src != nullptr)
    {
        const auto plan = dogside::computePlan (snap.markers, (double) snap.src->numSamples(), snap.settings);
        if (! plan.valid())
            return;

        result = std::make_shared<dogside::RenderResult>();
        const double bpm = juce::jlimit (20.0, 400.0, hostBpmAtomic.load());

        if (! dogside::renderWarp (*snap.src, plan, currentSampleRate.load(), bpm, snap.mode, *result, &renderRequested))
            return;   // cancelled by a newer request (or failed)
    }

    std::shared_ptr<const dogside::RenderResult> toDrop;
    {
        const juce::SpinLock::ScopedLockType sl (publishLock);
        published = result;
        history.push_back (result);
        if (history.size() > 4)
        {
            toDrop = history.front();
            history.pop_front();
        }
    }
    publishedVersion.fetch_add (1);
    toDrop.reset();   // freed here, never on the audio thread
}

std::shared_ptr<const dogside::RenderResult> AutoWarpAudioProcessor::getRendered() const
{
    const juce::SpinLock::ScopedLockType sl (publishLock);
    return published;
}

//==============================================================================
// Audio
//==============================================================================
void AutoWarpAudioProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate.store (sampleRate);
    lastRequestedBpm = (double) apvts.getRawParameterValue ("fallbackBpm")->load();
    hostBpmAtomic.store (lastRequestedBpm);

    voiceActive = false;
    previewVoice = false;
    previewActive.store (false);
    expectedLoopPos = -1.0;
    requestRender();
}

void AutoWarpAudioProcessor::renderSpan (juce::AudioBuffer<float>& buffer, int start, int num, double bpm, double sr,
                                         bool hostPlaying, bool hasPpq, double ppq, bool loop, bool syncMode, float gain)
{
    const auto* r = audioRender.get();
    if (r == nullptr || r->numSamples() < 2 || r->loopBeats <= 0.0 || num <= 0 || r->ch.empty())
        return;

    const double loopBeats = r->loopBeats;
    const double bps = bpm / (60.0 * sr);                 // beats per sample
    const long   numS = (long) r->numSamples();
    const int    outCh = buffer.getNumChannels();
    const bool   useVoice = previewVoice || ! syncMode;
    const bool   useHost = ! useVoice && hostPlaying && hasPpq;

    if (! useVoice && ! useHost)
    {
        expectedLoopPos = -1.0;
        return;
    }

    float* out[2] = { buffer.getWritePointer (0), outCh > 1 ? buffer.getWritePointer (1) : nullptr };
    const std::vector<float>* src[2] = { &r->ch[0], &r->ch[std::min<size_t> (1, r->ch.size() - 1)] };

    const double hostBeat0 = ppq + (double) start * bps;

    for (int i = 0; i < num; ++i)
    {
        double lp;

        if (useHost)
        {
            const double b = hostBeat0 + (double) i * bps;
            if (loop)
            {
                lp = std::fmod (b, loopBeats);
                if (lp < 0.0) lp += loopBeats;
            }
            else
            {
                if (b < 0.0 || b >= loopBeats) { expectedLoopPos = -1.0; continue; }
                lp = b;
            }
        }
        else
        {
            if (! voiceActive) break;

            if (voiceBeat >= loopBeats)
            {
                if (loop) voiceBeat = std::fmod (voiceBeat, loopBeats);
                else { voiceActive = false; previewVoice = false; previewActive.store (false); break; }
            }
            lp = voiceBeat;
            voiceBeat += bps;
        }

        // fade in after a jump (loop restart, host relocate, note on) to avoid clicks
        if (expectedLoopPos < 0.0)
            fadeRemaining = kFadeSamples;
        else
        {
            double d = std::abs (lp - expectedLoopPos);
            d = std::min (d, loopBeats - d);
            if (d > 3.0 * bps) fadeRemaining = kFadeSamples;
        }
        expectedLoopPos = lp + bps;
        if (expectedLoopPos >= loopBeats) expectedLoopPos -= loopBeats;
        lastLoopPos = lp;

        // read (linear interpolation)
        const double fp = lp / loopBeats * (double) numS;
        long i0 = (long) fp;
        const float fr = (float) (fp - (double) i0);
        if (i0 >= numS) i0 = numS - 1;
        long i1 = i0 + 1;
        if (i1 >= numS) i1 = loop ? 0 : i0;

        float g = gain * (useVoice ? voiceGain : 1.0f);
        if (fadeRemaining > 0)
        {
            g *= 1.0f - (float) fadeRemaining / (float) kFadeSamples;
            --fadeRemaining;
        }

        for (int c = 0; c < outCh && c < 2; ++c)
        {
            const auto& s = *src[c];
            out[c][start + i] += (s[(size_t) i0] + (s[(size_t) i1] - s[(size_t) i0]) * fr) * g;
        }
    }
}

void AutoWarpAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    buffer.clear();

    const double sr = getSampleRate();
    if (sr <= 0.0) return;

    // pick up the newest rendered loop
    const int version = publishedVersion.load();
    if (version != audioVersion)
    {
        const juce::SpinLock::ScopedTryLockType tl (publishLock);
        if (tl.isLocked())
        {
            audioRender = published;
            audioVersion = version;
        }
    }

    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    // host info
    double bpm = (double) get ("fallbackBpm");
    bool hostPlaying = false, hasPpq = false;
    double ppq = 0.0;

    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) if (*b > 1.0) bpm = *b;
            hostPlaying = pos->getIsPlaying();
            if (auto p = pos->getPpqPosition()) { ppq = *p; hasPpq = true; }
        }
    }

    // follow tempo changes (re-render the loop at the new tempo)
    if (std::abs (bpm - lastRequestedBpm) > 0.05)
    {
        lastRequestedBpm = bpm;
        hostBpmAtomic.store (bpm);
        requestRender();
    }

    const bool syncMode = ((int) std::lround (get ("playMode")) == 0);
    const bool loopParam = get ("loop") > 0.5f;
    const float gain = juce::Decibels::decibelsToGain (get ("gain"));

    // preview button
    const int cmd = previewCmd.exchange (0);
    if (cmd == 1)
    {
        previewVoice = true; voiceActive = true; voiceBeat = 0.0; voiceGain = 1.0f;
        expectedLoopPos = -1.0; previewActive.store (true);
    }
    else if (cmd == 2)
    {
        previewVoice = false; voiceActive = false; previewActive.store (false);
    }

    lastLoopPos = -1.0;

    // MIDI: sample-accurate note triggers when in "MIDI Trigger" mode
    int pos = 0;
    auto renderUntil = [&] (int upTo)
    {
        if (upTo > pos)
        {
            renderSpan (buffer, pos, upTo - pos, bpm, sr, hostPlaying, hasPpq, ppq,
                        loopParam || previewVoice, syncMode, gain);
            pos = upTo;
        }
    };

    if (! syncMode)
    {
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            const int t = juce::jlimit (0, numSamples, meta.samplePosition);

            if (msg.isNoteOn())
            {
                renderUntil (t);
                previewVoice = false; previewActive.store (false);
                voiceActive = true; voiceBeat = 0.0; voiceGain = msg.getFloatVelocity();
                expectedLoopPos = -1.0;
            }
            else if ((msg.isNoteOff() || msg.isAllNotesOff() || msg.isAllSoundOff()) && loopParam && ! previewVoice)
            {
                renderUntil (t);
                voiceActive = false;
            }
        }
    }

    renderUntil (numSamples);
    playheadBeat.store (lastLoopPos);
}

//==============================================================================
// Sample / marker management (message thread)
//==============================================================================
std::shared_ptr<const dogside::AudioData> AutoWarpAudioProcessor::getSource() const
{
    const juce::ScopedLock sl (dataLock);
    return source;
}

juce::String AutoWarpAudioProcessor::getSampleName() const
{
    const juce::ScopedLock sl (dataLock);
    return sampleName;
}

void AutoWarpAudioProcessor::setSource (std::shared_ptr<const dogside::AudioData> data,
                                        const juce::String& name, const juce::String& path)
{
    {
        const juce::ScopedLock sl (dataLock);
        source = std::move (data);
        markers.clear();
        startSample = 0.0;
        endSample = 0.0;
        sampleName = name;
        samplePath = path;
    }
    requestRender();
}

bool AutoWarpAudioProcessor::loadFile (const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
        return false;

    if ((double) reader->lengthInSamples > reader->sampleRate * 600.0)   // max 10 minutes
        return false;

    const int nch = (int) juce::jmin (2u, reader->numChannels);
    const int n = (int) reader->lengthInSamples;

    juce::AudioBuffer<float> tmp (nch, n);
    if (! reader->read (&tmp, 0, n, 0, true, nch > 1))
        return false;

    auto data = std::make_shared<dogside::AudioData>();
    data->sampleRate = reader->sampleRate;
    data->ch.resize ((size_t) nch);
    for (int c = 0; c < nch; ++c)
        data->ch[(size_t) c].assign (tmp.getReadPointer (c), tmp.getReadPointer (c) + n);

    setSource (data, file.getFileNameWithoutExtension(), file.getFullPathName());
    autoWarp();
    return true;
}

void AutoWarpAudioProcessor::autoWarp()
{
    const auto src = getSource();
    if (src == nullptr) return;

    const double sens = (double) apvts.getRawParameterValue ("sensitivity")->load() / 100.0;
    const auto r = dogside::autoWarp (*src, sens);

    {
        const juce::ScopedLock sl (dataLock);
        markers = r.markers;
        startSample = r.startSample;
        endSample = r.endSample;
    }
    setParam ("loopBeats", (float) r.loopBeats);
    requestRender();
}

std::vector<dogside::Marker> AutoWarpAudioProcessor::getMarkers() const
{
    const juce::ScopedLock sl (dataLock);
    return markers;
}

double AutoWarpAudioProcessor::getLoopStart() const
{
    const juce::ScopedLock sl (dataLock);
    return startSample;
}

double AutoWarpAudioProcessor::getLoopEnd() const
{
    const juce::ScopedLock sl (dataLock);
    if (endSample > 0.0) return endSample;
    return source != nullptr ? (double) source->numSamples() : 0.0;
}

void AutoWarpAudioProcessor::setLoopRange (double start, double end)
{
    {
        const juce::ScopedLock sl (dataLock);
        if (source == nullptr) return;
        const double len = (double) source->numSamples();
        start = juce::jlimit (0.0, len - 2048.0, start);
        end = juce::jlimit (start + 2048.0, len, end);
        startSample = start;
        endSample = end;
    }
    requestRender();
}

void AutoWarpAudioProcessor::addMarker (double sample)
{
    {
        const juce::ScopedLock sl (dataLock);
        if (source == nullptr) return;

        const double sr = source->sampleRate;
        const double lo = startSample + 16.0;
        const double hi = (endSample > 0.0 ? endSample : (double) source->numSamples()) - 16.0;
        sample = juce::jlimit (lo, hi, sample);

        // snap to the closest zero crossing so the cut doesn't click
        const auto& x = source->ch[0];
        const long c0 = (long) sample;
        long best = c0;
        for (long k = 0; k < 400; ++k)
        {
            bool found = false;
            for (long cand : { c0 - k, c0 + k })
            {
                if (cand < 1 || cand >= (long) x.size()) continue;
                if ((x[(size_t) cand] <= 0.0f && x[(size_t) cand - 1] > 0.0f)
                    || (x[(size_t) cand] >= 0.0f && x[(size_t) cand - 1] < 0.0f))
                {
                    best = cand; found = true; break;
                }
            }
            if (found) break;
        }
        sample = (double) best;

        for (const auto& m : markers)
            if (std::abs (m.sample - sample) < 0.005 * sr)
                return;   // too close to an existing cut

        dogside::Marker m;
        m.sample = sample;
        markers.push_back (m);
        std::stable_sort (markers.begin(), markers.end(),
                          [] (const dogside::Marker& a, const dogside::Marker& b) { return a.sample < b.sample; });
    }
    requestRender();
}

void AutoWarpAudioProcessor::removeMarker (int index)
{
    {
        const juce::ScopedLock sl (dataLock);
        if (index < 0 || index >= (int) markers.size()) return;
        markers.erase (markers.begin() + index);
    }
    requestRender();
}

int AutoWarpAudioProcessor::moveMarker (int index, double newSample)
{
    int result = index;
    {
        const juce::ScopedLock sl (dataLock);
        if (index < 0 || index >= (int) markers.size() || source == nullptr) return index;

        const double hi = (endSample > 0.0 ? endSample : (double) source->numSamples()) - 16.0;
        newSample = juce::jlimit (startSample + 16.0, hi, newSample);
        markers[(size_t) index].sample = newSample;

        std::stable_sort (markers.begin(), markers.end(),
                          [] (const dogside::Marker& a, const dogside::Marker& b) { return a.sample < b.sample; });

        for (int i = 0; i < (int) markers.size(); ++i)
            if (markers[(size_t) i].sample == newSample) { result = i; break; }
    }
    requestRender();
    return result;
}

void AutoWarpAudioProcessor::setMarkerManual (int index, bool manual, double beat)
{
    {
        const juce::ScopedLock sl (dataLock);
        if (index < 0 || index >= (int) markers.size()) return;
        markers[(size_t) index].manual = manual;
        if (manual) markers[(size_t) index].manualBeat = beat;
    }
    requestRender();
}

void AutoWarpAudioProcessor::clearMarkers()
{
    {
        const juce::ScopedLock sl (dataLock);
        markers.clear();
    }
    requestRender();
}

dogside::WarpPlan AutoWarpAudioProcessor::getPlan() const
{
    const Snapshot s = takeSnapshot();
    if (s.src == nullptr) return {};
    return dogside::computePlan (s.markers, (double) s.src->numSamples(), s.settings);
}

double AutoWarpAudioProcessor::getGridBeats() const
{
    return dogside::gridBeatsFromIndex ((int) std::lround (apvts.getRawParameterValue ("grid")->load()));
}

//==============================================================================
bool AutoWarpAudioProcessor::exportWav (const juce::File& file)
{
    const auto r = getRendered();
    if (r == nullptr || r->numSamples() == 0)
        return false;

    file.deleteFile();
    auto fos = std::make_unique<juce::FileOutputStream> (file);
    if (! fos->openedOk())
        return false;

    std::unique_ptr<juce::OutputStream> stream = std::move (fos);   // the writer takes ownership of it

    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                   .withSampleRate (r->sampleRate)
                                                   .withNumChannels ((int) r->ch.size())
                                                   .withBitsPerSample (24));
    if (writer == nullptr)
        return false;

    std::vector<const float*> ptrs;
    for (const auto& c : r->ch) ptrs.push_back (c.data());
    return writer->writeFromFloatArrays (ptrs.data(), (int) ptrs.size(), (int) r->numSamples());
}

//==============================================================================
// State (saved inside the DAW project: parameters, cuts, and the audio itself)
//==============================================================================
void AutoWarpAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root ("AUTOWARP");
    root.setProperty ("version", 1, nullptr);
    root.setProperty ("madeBy", juce::String (AUTOWARP_CREDITS_COMPANY) + " / " + AUTOWARP_CREDITS_AUTHOR, nullptr);
    root.appendChild (apvts.copyState(), nullptr);

    std::shared_ptr<const dogside::AudioData> src;
    {
        const juce::ScopedLock sl (dataLock);
        src = source;
        root.setProperty ("name", sampleName, nullptr);
        root.setProperty ("path", samplePath, nullptr);
        root.setProperty ("start", startSample, nullptr);
        root.setProperty ("end", endSample, nullptr);

        juce::ValueTree mk ("MARKERS");
        for (const auto& m : markers)
        {
            juce::ValueTree v ("M");
            v.setProperty ("pos", m.sample, nullptr);
            v.setProperty ("manual", m.manual, nullptr);
            v.setProperty ("beat", m.manualBeat, nullptr);
            mk.appendChild (v, nullptr);
        }
        root.appendChild (mk, nullptr);
    }

    if (src != nullptr && ! src->ch.empty())
    {
        const size_t n = src->numSamples();
        const size_t bytes = n * src->ch.size() * sizeof (float);
        if (bytes < 200u * 1024u * 1024u)
        {
            juce::MemoryBlock blob;
            {
                juce::MemoryOutputStream o (blob, false);
                o.writeInt (kAudioMagic);
                o.writeInt ((int) src->ch.size());
                o.writeDouble (src->sampleRate);
                o.writeInt64 ((juce::int64) n);
                for (const auto& c : src->ch)
                    o.write (c.data(), n * sizeof (float));
            }
            root.setProperty ("audio", juce::var (blob), nullptr);
        }
    }

    juce::MemoryOutputStream mos (destData, false);
    root.writeToStream (mos);
}

void AutoWarpAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto root = juce::ValueTree::readFromData (data, (size_t) sizeInBytes);
    if (! root.isValid() || ! root.hasType ("AUTOWARP"))
        return;

    const auto params = root.getChildWithName (apvts.state.getType());
    if (params.isValid())
        apvts.replaceState (params);

    // audio
    std::shared_ptr<dogside::AudioData> audio;
    if (auto* blob = root.getProperty ("audio").getBinaryData())
    {
        juce::MemoryInputStream in (*blob, false);
        if (in.readInt() == kAudioMagic)
        {
            const int nch = in.readInt();
            const double sr = in.readDouble();
            const auto n = (size_t) in.readInt64();
            if (nch >= 1 && nch <= 2 && sr > 0.0 && n > 0 && n * (size_t) nch * sizeof (float) <= blob->getSize())
            {
                audio = std::make_shared<dogside::AudioData>();
                audio->sampleRate = sr;
                audio->ch.assign ((size_t) nch, std::vector<float> (n));
                for (auto& c : audio->ch)
                    in.read (c.data(), (int) (n * sizeof (float)));
            }
        }
    }

    {
        const juce::ScopedLock sl (dataLock);
        source = audio;
        sampleName = root.getProperty ("name").toString();
        samplePath = root.getProperty ("path").toString();
        startSample = (double) root.getProperty ("start", 0.0);
        endSample = (double) root.getProperty ("end", 0.0);

        markers.clear();
        const auto mk = root.getChildWithName ("MARKERS");
        for (int i = 0; i < mk.getNumChildren(); ++i)
        {
            const auto v = mk.getChild (i);
            dogside::Marker m;
            m.sample = (double) v.getProperty ("pos", 0.0);
            m.manual = (bool) v.getProperty ("manual", false);
            m.manualBeat = (double) v.getProperty ("beat", 0.0);
            markers.push_back (m);
        }
    }

    // the audio was not embedded (very long file): try the original path
    if (audio == nullptr)
    {
        const juce::File f (root.getProperty ("path").toString());
        if (f.existsAsFile())
        {
            const auto savedMarkers = getMarkers();
            const double s = getLoopStart(), e = getLoopEnd();
            if (loadFile (f))
            {
                {
                    const juce::ScopedLock sl (dataLock);
                    markers = savedMarkers;
                    startSample = s;
                    endSample = e;
                }
            }
        }
    }

    requestRender();
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AutoWarpAudioProcessor();
}
