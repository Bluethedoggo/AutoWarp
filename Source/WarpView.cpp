// AutoWarp
// Created by DogSide Productions & Bluethedoggo
#include "WarpView.h"

#include <cmath>

namespace
{
    juce::Font fontOf (float h, bool bold = false)
    {
        return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
    }
}

WarpView::WarpView (AutoWarpAudioProcessor& p) : processor (p)
{
    setOpaque (true);
    startTimerHz (30);
}

WarpView::~WarpView() { stopTimer(); }

//==============================================================================
void WarpView::resized()
{
    auto r = getLocalBounds();
    const int gap = 44;
    const int topH = (r.getHeight() - gap) * 5 / 11;
    topLane = r.removeFromTop (topH);
    r.removeFromTop (gap);
    bottomLane = r;
    peakW = -1;
    bottomDirty = true;
}

void WarpView::zoomFit()
{
    visStart = 0.0;
    visLen = src != nullptr ? (double) src->numSamples() : 1.0;
    peakW = -1;
}

void WarpView::refreshCache()
{
    src = processor.getSource();
    rendered = processor.getRendered();
    plan = processor.getPlan();
    loopStart = processor.getLoopStart();
    loopEnd = processor.getLoopEnd();
    gridBeats = processor.getGridBeats();
    playBeat = processor.getPlayheadBeat();

    if (src.get() != lastSrcPtr)
    {
        lastSrcPtr = src.get();
        zoomFit();
    }
    if (rendered.get() != lastRenderPtr)
    {
        lastRenderPtr = rendered.get();
        bottomDirty = true;
    }
}

void WarpView::timerCallback()
{
    refreshCache();
    repaint();
}

//==============================================================================
double WarpView::xToSample (float x) const
{
    const double w = (double) juce::jmax (1, topLane.getWidth());
    return visStart + (double) x / w * visLen;
}

float WarpView::sampleToX (double s) const
{
    const double w = (double) juce::jmax (1, topLane.getWidth());
    return (float) ((s - visStart) / visLen * w);
}

float WarpView::beatToX (double beat) const
{
    const double lb = plan.valid() ? plan.loopBeats : 1.0;
    return (float) (beat / lb * (double) bottomLane.getWidth());
}

double WarpView::xToBeat (float x) const
{
    const double lb = plan.valid() ? plan.loopBeats : 1.0;
    return (double) x / (double) juce::jmax (1, bottomLane.getWidth()) * lb;
}

//==============================================================================
void WarpView::rebuildPeaks()
{
    const int w = topLane.getWidth();
    topPeaks.assign ((size_t) juce::jmax (0, w), { 0.0f, 0.0f });

    if (src != nullptr && ! src->ch.empty())
    {
        const long n = (long) src->numSamples();
        const size_t nch = src->ch.size();
        for (int px = 0; px < w; ++px)
        {
            const double a = visStart + (double) px * visLen / (double) w;
            const double b = visStart + (double) (px + 1) * visLen / (double) w;
            const long ia = juce::jmax (0L, (long) a);
            const long ib = juce::jmin (n, (long) std::ceil (b) + 1);
            const long step = juce::jmax (1L, (ib - ia) / 48);
            float mn = 1.0e9f, mx = -1.0e9f;
            for (long i = ia; i < ib; i += step)
            {
                float v = 0.0f;
                for (size_t c = 0; c < nch; ++c) v += src->ch[c][(size_t) i];
                v /= (float) nch;
                mn = juce::jmin (mn, v);
                mx = juce::jmax (mx, v);
            }
            if (mn <= mx) topPeaks[(size_t) px] = { mn, mx };
        }
    }

    peakStart = visStart; peakLen = visLen; peakW = w;
}

//==============================================================================
void WarpView::paint (juce::Graphics& g)
{
    g.fillAll (palette::bg);

    if (topLane.isEmpty() || bottomLane.isEmpty())
        return;

    if (peakW != topLane.getWidth() || peakStart != visStart || peakLen != visLen)
        rebuildPeaks();

    const int w = topLane.getWidth();

    // ---------- lanes ----------
    g.setColour (palette::lane);
    g.fillRoundedRectangle (topLane.toFloat(), 6.0f);
    g.fillRoundedRectangle (bottomLane.toFloat(), 6.0f);

    g.setFont (fontOf (11.0f, true));
    g.setColour (palette::dim);
    g.drawText ("SOURCE  -  double-click to add a cut  -  drag to move  -  right-click / Alt-click to delete  -  mouse wheel to zoom",
                topLane.reduced (10, 4).removeFromTop (14), juce::Justification::topLeft);
    g.drawText ("WARPED LOOP  -  drag a marker to place a hit by hand (Shift = free)  -  right-click a marker to reset it",
                bottomLane.reduced (10, 4).removeFromTop (14), juce::Justification::topLeft);

    if (src == nullptr)
    {
        g.setColour (palette::text);
        g.setFont (fontOf (18.0f, true));
        g.drawText ("Drop an audio file here, or press LOAD", getLocalBounds().withHeight (getHeight()), juce::Justification::centred);

        if (dragOver)
        {
            g.setColour (palette::accent);
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), 8.0f, 2.0f);
        }
        return;
    }

    // ---------- top lane: waveform ----------
    {
        const float midY = (float) topLane.getCentreY() + 6.0f;
        const float half = (float) topLane.getHeight() * 0.5f - 22.0f;

        g.setColour (palette::accent.withAlpha (0.85f));
        for (int px = 0; px < w && px < (int) topPeaks.size(); ++px)
        {
            const auto pk = topPeaks[(size_t) px];
            g.drawVerticalLine (topLane.getX() + px, midY - juce::jlimit (-1.0f, 1.0f, pk.second) * half,
                                midY - juce::jlimit (-1.0f, 1.0f, pk.first) * half + 1.0f);
        }

        // dim everything outside the loop region
        const float xs = juce::jlimit (0.0f, (float) w, sampleToX (loopStart));
        const float xe = juce::jlimit (0.0f, (float) w, sampleToX (loopEnd));
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRect ((float) topLane.getX(), (float) topLane.getY(), xs, (float) topLane.getHeight());
        g.fillRect ((float) topLane.getX() + xe, (float) topLane.getY(), (float) w - xe, (float) topLane.getHeight());

        // loop edges
        auto drawEdge = [&] (float x, const juce::String& label, bool isStart)
        {
            if (x < -2.0f || x > (float) w + 2.0f) return;
            g.setColour (palette::loopEdge);
            g.drawLine (x, (float) topLane.getY(), x, (float) topLane.getBottom(), 2.0f);
            g.setFont (fontOf (10.0f, true));
            g.drawText (label, juce::Rectangle<float> (isStart ? x + 3.0f : x - 43.0f, (float) topLane.getBottom() - 16.0f, 40.0f, 12.0f),
                        isStart ? juce::Justification::left : juce::Justification::right);
        };
        drawEdge (sampleToX (loopStart), "START", true);
        drawEdge (sampleToX (loopEnd), "END", false);
    }

    // ---------- warp lines between the lanes ----------
    if (plan.valid())
    {
        for (const auto& p : plan.points)
        {
            const float x0 = sampleToX (p.sample);
            const float x1 = beatToX (p.targetBeat);
            if (x0 < -50.0f || x0 > (float) w + 50.0f) continue;

            const bool anchor = p.markerIndex < 0;
            g.setColour ((p.manual ? palette::manual : (anchor ? palette::loopEdge : palette::accent2)).withAlpha (anchor ? 0.5f : 0.45f));
            g.drawLine (x0, (float) topLane.getBottom(), x1, (float) bottomLane.getY(), 1.0f);
        }
    }

    // ---------- top lane: cuts ----------
    if (plan.valid())
    {
        for (const auto& p : plan.points)
        {
            if (p.markerIndex < 0) continue;
            const float x = sampleToX (p.sample);
            if (x < -6.0f || x > (float) w + 6.0f) continue;

            const auto col = p.manual ? palette::manual : palette::accent2;
            g.setColour (col);
            g.drawLine (x, (float) topLane.getY() + 18.0f, x, (float) topLane.getBottom(), 1.5f);

            juce::Path tri;
            tri.addTriangle (x - 6.0f, (float) topLane.getY() + 18.0f, x + 6.0f, (float) topLane.getY() + 18.0f, x, (float) topLane.getY() + 30.0f);
            g.fillPath (tri);
        }
    }

    // ---------- bottom lane: beat grid + rendered loop ----------
    {
        const double lb = plan.valid() ? plan.loopBeats : 1.0;
        const float bw = (float) bottomLane.getWidth();
        const float top = (float) bottomLane.getY();
        const float bot = (float) bottomLane.getBottom();

        // sub-grid
        const float gridPx = (float) (gridBeats / lb * (double) bw);
        if (gridPx >= 5.0f)
        {
            g.setColour (palette::dim.withAlpha (0.12f));
            for (double b = 0.0; b <= lb + 1.0e-6; b += gridBeats)
                g.drawVerticalLine ((int) std::lround (beatToX (b)), top, bot);
        }
        // beats and bars
        for (int b = 0; b <= (int) std::lround (lb); ++b)
        {
            const bool bar = (b % 4 == 0);
            if (! bar && (float) bw / (float) lb < 8.0f) continue;
            g.setColour (palette::dim.withAlpha (bar ? 0.55f : 0.25f));
            g.drawVerticalLine ((int) std::lround (beatToX ((double) b)), top + (bar ? 0.0f : 18.0f), bot);
        }

        // waveform of the rendered result
        if (rendered != nullptr && rendered->numSamples() > 1)
        {
            if (bottomDirty || (int) bottomPeaks.size() != bottomLane.getWidth())
            {
                const int bwi = bottomLane.getWidth();
                bottomPeaks.assign ((size_t) bwi, { 0.0f, 0.0f });
                const long n = (long) rendered->numSamples();
                for (int px = 0; px < bwi; ++px)
                {
                    const long ia = (long) ((double) px * (double) n / (double) bwi);
                    const long ib = juce::jmin (n, (long) ((double) (px + 1) * (double) n / (double) bwi) + 1);
                    const long step = juce::jmax (1L, (ib - ia) / 48);
                    float mn = 1.0e9f, mx = -1.0e9f;
                    for (long i = ia; i < ib; i += step)
                    {
                        float v = 0.0f;
                        for (const auto& c : rendered->ch) v += c[(size_t) i];
                        v /= (float) rendered->ch.size();
                        mn = juce::jmin (mn, v); mx = juce::jmax (mx, v);
                    }
                    if (mn <= mx) bottomPeaks[(size_t) px] = { mn, mx };
                }
                bottomDirty = false;
            }

            const float midY = (float) bottomLane.getCentreY() + 8.0f;
            const float half = (float) bottomLane.getHeight() * 0.5f - 24.0f;
            g.setColour (palette::accent.withAlpha (0.9f));
            for (int px = 0; px < (int) bottomPeaks.size(); ++px)
            {
                const auto pk = bottomPeaks[(size_t) px];
                g.drawVerticalLine (bottomLane.getX() + px, midY - juce::jlimit (-1.0f, 1.0f, pk.second) * half,
                                    midY - juce::jlimit (-1.0f, 1.0f, pk.first) * half + 1.0f);
            }
        }

        // warp markers (draggable handles)
        if (plan.valid())
        {
            for (const auto& p : plan.points)
            {
                if (p.markerIndex < 0) continue;
                const float x = beatToX (p.targetBeat);
                g.setColour (p.manual ? palette::manual : palette::accent2);
                g.drawLine (x, top + 18.0f, x, bot, 1.5f);
                g.fillEllipse (x - 5.0f, top + 14.0f, 10.0f, 10.0f);
            }
        }
    }

    // ---------- playhead ----------
    if (plan.valid() && playBeat >= 0.0)
    {
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        const float bx = beatToX (playBeat);
        g.drawLine (bx, (float) bottomLane.getY(), bx, (float) bottomLane.getBottom(), 1.5f);

        const float tx = sampleToX (dogside::planBeatToSample (plan, playBeat));
        if (tx >= 0.0f && tx <= (float) w)
            g.drawLine (tx, (float) topLane.getY(), tx, (float) topLane.getBottom(), 1.5f);
    }

    if (dragOver)
    {
        g.setColour (palette::accent);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), 8.0f, 2.0f);
    }
}

//==============================================================================
int WarpView::hitTopMarker (float x) const
{
    int best = -1; float bestD = 8.0f;
    for (const auto& p : plan.points)
    {
        if (p.markerIndex < 0) continue;
        const float d = std::abs (x - sampleToX (p.sample));
        if (d < bestD) { bestD = d; best = p.markerIndex; }
    }
    return best;
}

int WarpView::hitBottomMarker (float x) const
{
    int best = -1; float bestD = 8.0f;
    for (const auto& p : plan.points)
    {
        if (p.markerIndex < 0) continue;
        const float d = std::abs (x - beatToX (p.targetBeat));
        if (d < bestD) { bestD = d; best = p.markerIndex; }
    }
    return best;
}

bool WarpView::hitLoopEdge (float x, bool start) const
{
    return std::abs (x - sampleToX (start ? loopStart : loopEnd)) < 7.0f;
}

void WarpView::mouseDown (const juce::MouseEvent& e)
{
    refreshCache();
    dragMode = Drag::None;
    dragIndex = -1;
    if (src == nullptr) return;

    const auto p = e.position;

    if (topLane.contains (p.toInt()))
    {
        const int m = hitTopMarker (p.x);

        if (e.mods.isPopupMenu() || e.mods.isAltDown())
        {
            if (m >= 0) processor.removeMarker (m);
            return;
        }

        if (m >= 0)                     { dragMode = Drag::Marker; dragIndex = m; }
        else if (hitLoopEdge (p.x, true))  dragMode = Drag::Start;
        else if (hitLoopEdge (p.x, false)) dragMode = Drag::End;
    }
    else if (bottomLane.contains (p.toInt()))
    {
        const int m = hitBottomMarker (p.x);
        if (m < 0) return;

        if (e.mods.isPopupMenu() || e.mods.isAltDown())
            processor.setMarkerManual (m, false, 0.0);
        else
        {
            dragMode = Drag::Target;
            dragIndex = m;
        }
    }
}

void WarpView::mouseDrag (const juce::MouseEvent& e)
{
    if (src == nullptr || dragMode == Drag::None) return;

    switch (dragMode)
    {
        case Drag::Marker:
            dragIndex = processor.moveMarker (dragIndex, xToSample (e.position.x));
            break;

        case Drag::Start:
            processor.setLoopRange (xToSample (e.position.x), loopEnd);
            break;

        case Drag::End:
            processor.setLoopRange (loopStart, xToSample (e.position.x));
            break;

        case Drag::Target:
        {
            double beat = juce::jlimit (0.0, plan.loopBeats, xToBeat (e.position.x));
            if (! e.mods.isShiftDown() && gridBeats > 0.0)
                beat = std::round (beat / gridBeats) * gridBeats;
            processor.setMarkerManual (dragIndex, true, beat);
            break;
        }

        case Drag::None: break;
    }
}

void WarpView::mouseUp (const juce::MouseEvent&)
{
    dragMode = Drag::None;
    dragIndex = -1;
}

void WarpView::mouseMove (const juce::MouseEvent& e)
{
    if (src == nullptr) { setMouseCursor (juce::MouseCursor::NormalCursor); return; }

    const auto p = e.position;
    bool onHandle = false;
    if (topLane.contains (p.toInt()))
        onHandle = hitTopMarker (p.x) >= 0 || hitLoopEdge (p.x, true) || hitLoopEdge (p.x, false);
    else if (bottomLane.contains (p.toInt()))
        onHandle = hitBottomMarker (p.x) >= 0;

    setMouseCursor (onHandle ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}

void WarpView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (src == nullptr) return;
    refreshCache();

    if (topLane.contains (e.getPosition()) && hitTopMarker (e.position.x) < 0
        && ! hitLoopEdge (e.position.x, true) && ! hitLoopEdge (e.position.x, false))
    {
        processor.addMarker (xToSample (e.position.x));
    }
}

void WarpView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (src == nullptr || ! topLane.contains (e.getPosition())) return;

    const double total = (double) src->numSamples();
    const double w = (double) juce::jmax (1, topLane.getWidth());

    if (e.mods.isShiftDown() || std::abs (wheel.deltaX) > std::abs (wheel.deltaY))
    {
        const double d = (std::abs (wheel.deltaX) > 0.0f ? wheel.deltaX : wheel.deltaY);
        visStart = juce::jlimit (0.0, juce::jmax (0.0, total - visLen), visStart - d * visLen * 0.5);
    }
    else
    {
        const double anchor = xToSample (e.position.x);
        const double zoom = std::exp (-(double) wheel.deltaY * 1.5);
        visLen = juce::jlimit (256.0, total, visLen * zoom);
        visStart = anchor - (double) e.position.x / w * visLen;
        visStart = juce::jlimit (0.0, juce::jmax (0.0, total - visLen), visStart);
    }
}

//==============================================================================
bool WarpView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg"))
            return true;
    return false;
}

void WarpView::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    for (auto& f : files)
    {
        if (! juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg")) continue;

        if (! processor.loadFile (juce::File (f)))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "AutoWarp",
                                                    "Could not read that audio file.");
        break;
    }
    repaint();
}
