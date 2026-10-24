#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
// A custom LookAndFeel for the WaveSlave UI
class WaveSlaveLookAndFeel : public juce::LookAndFeel_V4
{
public:
    WaveSlaveLookAndFeel()
    {
        // Dark theme palette
        setColour(juce::Slider::rotarySliderFillColourId,    juce::Colour(0xFFFFFFFF)); // cyan accent
        setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xFF333333));
        setColour(juce::Slider::thumbColourId,               juce::Colour(0xFFFFFFFF));
        setColour(juce::Slider::trackColourId,               juce::Colour(0xFFFFFFFF));
        setColour(juce::Slider::backgroundColourId,          juce::Colour(0xFF333333));
        setColour(juce::Slider::textBoxTextColourId,         juce::Colours::white);
        setColour(juce::Slider::textBoxOutlineColourId,      juce::Colour(0x00000000)); // transparent outline
        setColour(juce::ComboBox::backgroundColourId,        juce::Colour(0xFF333333));
        setColour(juce::ComboBox::textColourId,              juce::Colours::white);
        setColour(juce::ComboBox::outlineColourId,           juce::Colour(0xFF555555));
        setColour(juce::ComboBox::arrowColourId,             juce::Colour(0xFFFFFFFF));
        setColour(juce::PopupMenu::backgroundColourId,       juce::Colour(0xFF222222));
        setColour(juce::PopupMenu::textColourId,             juce::Colours::white);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xFFFFFFFF));
        setColour(juce::PopupMenu::highlightedTextColourId,  juce::Colours::black);
        setColour(juce::ScrollBar::thumbColourId,            juce::Colour(0xFFFFFFFF).withAlpha(0.5f));
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPosProportional, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider& slider) override
    {
        juce::ignoreUnused(slider);
        auto radius = (float)juce::jmin(width / 2, height / 2) - 6.0f;
        auto centreX = (float)x + (float)width  * 0.5f;
        auto centreY = (float)y + (float)height * 0.5f;
        auto angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

        // Background arc
        juce::Path bgArc;
        bgArc.addCentredArc(centreX, centreY, radius, radius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
        g.setColour(juce::Colour(0xFF333333));
        g.strokePath(bgArc, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Value arc
        juce::Path valueArc;
        valueArc.addCentredArc(centreX, centreY, radius, radius, 0.0f, rotaryStartAngle, angle, true);
        g.setColour(juce::Colour(0xFFFFFFFF));
        g.strokePath(valueArc, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Thumb dot
        juce::Path thumb;
        auto thumbWidth = 6.0f;
        thumb.addEllipse(-thumbWidth / 2, -radius - 1.0f, thumbWidth, thumbWidth);
        g.setColour(juce::Colours::white);
        g.fillPath(thumb, juce::AffineTransform::rotation(angle).translated(centreX, centreY));
    }

    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          const juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        if (style == juce::Slider::LinearVertical)
        {
            auto trackWidth = 4.0f;
            auto centreX = (float)x + (float)width * 0.5f;
            auto top = (float)y;
            auto bottom = (float)y + (float)height;

            // Track background
            g.setColour(juce::Colour(0xFF333333));
            g.fillRoundedRectangle(centreX - trackWidth * 0.5f, top, trackWidth, bottom - top, 2.0f);

            // Filled portion
            g.setColour(juce::Colour(0xFFFFFFFF));
            g.fillRoundedRectangle(centreX - trackWidth * 0.5f, sliderPos, trackWidth, bottom - sliderPos, 2.0f);

            // Thumb
            g.setColour(juce::Colours::white);
            g.fillEllipse(centreX - 5.0f, sliderPos - 5.0f, 10.0f, 10.0f);
        }
        else if (style == juce::Slider::LinearHorizontal || style == juce::Slider::LinearBar)
        {
            auto trackHeight = 4.0f;
            auto centreY = (float)y + (float)height * 0.5f;
            auto left = (float)x;
            auto right = (float)x + (float)width;

            // Track background
            g.setColour(juce::Colour(0xFF333333));
            g.fillRoundedRectangle(left, centreY - trackHeight * 0.5f, right - left, trackHeight, 2.0f);

            // Filled portion
            g.setColour(juce::Colour(0xFFFFFFFF));
            g.fillRoundedRectangle(left, centreY - trackHeight * 0.5f, sliderPos - left, trackHeight, 2.0f);

            // Thumb
            g.setColour(juce::Colours::white);
            g.fillEllipse(sliderPos - 5.0f, centreY - 5.0f, 10.0f, 10.0f);
        }
        else
        {
            LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        }
    }

    juce::Font getTextButtonFont(juce::TextButton& button, int) override
    {
        if (button.getButtonText().length() >= 7 && button.getWidth() > 0 && button.getWidth() <= 48)
            return juce::Font(9.0f, juce::Font::bold);
        return juce::Font(10.5f, juce::Font::bold);
    }

    juce::Font getComboBoxFont(juce::ComboBox&) override
    {
        return juce::Font(11.0f);
    }

    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds(1, 1, box.getWidth() - 20, box.getHeight() - 2);
        label.setFont(getComboBoxFont(box));
    }

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                          bool /*shouldDrawButtonAsHighlighted*/, bool /*shouldDrawButtonAsDown*/) override
    {
        auto bounds = button.getLocalBounds().toFloat();
        float boxSize = 11.0f;
        float boxX = 3.0f;
        float boxY = (bounds.getHeight() - boxSize) * 0.5f;

        // Draw box border & background
        auto boxRect = juce::Rectangle<float>(boxX, boxY, boxSize, boxSize);
        g.setColour(juce::Colour(0xFF222222));
        g.fillRect(boxRect);
        g.setColour(button.getToggleState() ? juce::Colour(0xFFFFFFFF) : juce::Colour(0xFF555555));
        g.drawRect(boxRect, 1.0f);

        // Draw tick if checked
        if (button.getToggleState())
        {
            g.setColour(juce::Colour(0xFFFFFFFF));
            g.fillRect(boxRect.reduced(2.0f));
        }

        // Draw text with no ellipsis truncation
        float textX = boxX + boxSize + 4.0f;
        float textW = bounds.getWidth() - textX - 1.0f;
        g.setFont(juce::Font(10.0f, juce::Font::bold));
        g.setColour(button.getToggleState() ? juce::Colour(0xFFFFFFFF) : juce::Colour(0xFF888888));
        g.drawText(button.getButtonText(), (int)textX, 0, (int)textW, (int)bounds.getHeight(),
                   juce::Justification::centredLeft, false);
    }
};

inline juce::Colour getPartialColour(int index)
{
    static const juce::Colour palette[] = {
        juce::Colour(0xFF00E5FF), // 1: Cyan
        juce::Colour(0xFF00FF7F), // 2: Spring Green
        juce::Colour(0xFFFFEA00), // 3: Bright Yellow
        juce::Colour(0xFFFF6D00), // 4: Deep Orange
        juce::Colour(0xFFFF1744), // 5: Vibrant Red/Pink
        juce::Colour(0xFFD500F9), // 6: Neon Purple
        juce::Colour(0xFF2979FF), // 7: Royal Blue
        juce::Colour(0xFF1DE9B6), // 8: Teal
        juce::Colour(0xFFAEEA00), // 9: Lime
        juce::Colour(0xFFFFAB00), // 10: Amber
        juce::Colour(0xFFF50057), // 11: Rose
        juce::Colour(0xFFB388FF), // 12: Lavender
        juce::Colour(0xFF40C4FF), // 13: Sky Blue
        juce::Colour(0xFF69F0AE), // 14: Mint
        juce::Colour(0xFFFF9100), // 15: Bright Orange
        juce::Colour(0xFFFF5252)  // 16: Coral
    };
    return palette[juce::jlimit(0, 15, index)];
}

//==============================================================================
// Stylized Oscilloscope with glow, grid, rounded corners, and overlapping partials
class Oscilloscope : public juce::Component, public juce::Timer
{
public:
    Oscilloscope(WaveSlaveAudioProcessor& p);
    ~Oscilloscope() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;

    void setOverlaysEnabled(bool enabled) { showOverlays = enabled; repaint(); }
    bool areOverlaysEnabled() const { return showOverlays; }

    void setPolarMode(bool polar) { isPolar = polar; repaint(); }
    bool getPolarMode() const { return isPolar; }

    void clearWavetable(int slot = -1);
    void setSlot(int slotIndex);
    int getSlot() const { return currentSlot; }

    const std::vector<juce::Point<float>>& getSlotSplinePoints(int slot) const { return slotSplinePoints[juce::jlimit(0, 3, slot)]; }
    std::vector<juce::Point<float>>& getSlotSplinePoints(int slot) { return slotSplinePoints[juce::jlimit(0, 3, slot)]; }
    void setSlotSplinePoints(int slot, const std::vector<juce::Point<float>>& points) { slotSplinePoints[juce::jlimit(0, 3, slot)] = points; }

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    WaveSlaveAudioProcessor& processor;
    juce::AudioBuffer<float> displayBuffer;
    juce::AudioBuffer<float> partialDisplayBuffer;
    bool showOverlays = false;
    bool isPolar = false;
    bool isDrawMode = false;
    int currentSlot = 0; // 0..3 (Custom 1..4)

    juce::TextButton overlayBtn { "OVLY" };
    juce::TextButton polarBtn { "POLAR" };
    juce::TextButton drawBtn { "DRAW" };

    // Custom waveform editor controls (shown when DRAW is active)
    juce::ComboBox slotSelector;
    juce::ToggleButton bezierToggle { "Smooth" };
    juce::TextButton clearBtn { "CLR" };

    std::array<std::vector<juce::Point<float>>, 4> slotSplinePoints;
    int draggedPointIndex = -1;

    void updateWavetableFromSpline();
    void drawOnWavetable(const juce::MouseEvent& e, bool isDrag, bool isRelease);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Oscilloscope)
};

//==============================================================================
// Stylized Interactive Spectrograph with draggable harmonic nodes and glow
class Spectrograph : public juce::Component, public juce::Timer
{
public:
    Spectrograph(WaveSlaveAudioProcessor& p);
    ~Spectrograph() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;

    void mouseMove(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

    std::function<void(int)> onPartialSelected;
    std::function<void()> onAnimationChanged;
    void setSelectedPartial(int index);
    int getSelectedPartial() const { return selectedPartial; }

    void recordKeyframeFromSliders(int partialIndex);
    void syncTimelineControlsToSelectedPartial();
    float getScrubTimeNorm() const { return scrubTimeNorm; }

    void setTimelineComponent(juce::Component* comp);
    void setTimelineOpen(bool open);
    bool getTimelineOpen() const { return isTimelineOpen; }
    void paintKeyframeTimeline(juce::Graphics& g, juce::Rectangle<float> timelineArea);
    void layoutKeyframeTimeline(juce::Rectangle<int> bounds);
    void handleTimelineMouseMove(const juce::MouseEvent& e);
    void handleTimelineMouseDown(const juce::MouseEvent& e);
    void handleTimelineMouseDrag(const juce::MouseEvent& e);
    void handleTimelineMouseUp(const juce::MouseEvent& e);
    void handleTimelineMouseDoubleClick(const juce::MouseEvent& e);
    void syncSlidersToPlayhead(int partialIdx);

    void setFullCanvasMode(bool full) { isFullCanvas = full; repaint(); }
    bool isFullCanvasMode() const { return isFullCanvas; }

    enum class SpectrumScaleMode
    {
        Sliding,    // Note-tracking harmonic ratio scale
        Full        // Fixed 20 Hz - 20 kHz log frequency scale
    };

    SpectrumScaleMode getSpectrumScaleMode() const { return scaleMode; }
    bool isFullSpectrumView() const { return scaleMode == SpectrumScaleMode::Full; }
    void setSpectrumScaleMode(SpectrumScaleMode mode);

    void setNodesLocked(bool locked);
    bool getNodesLocked() const { return isLocked; }

    void setReadOnly(bool readOnly) { isReadOnly = readOnly; }
    bool isReadOnlyMode() const { return isReadOnly; }

    void setWaterfallMode(bool on)
    {
        isWaterfallMode = on;
        if (on)
        {
            // Reset image so it redraws cleanly
            waterfallImage = juce::Image();
            waterfallHistory.clear();
            waterfallLineY = 0;
            lastSeenTriggerCounter = -1;
        }
        resized();
        repaint();
    }
    bool isWaterfallModeActive() const { return isWaterfallMode; }

    enum class WaterfallPalette
    {
        Spectrum = 0,
        Aurora,
        Thermal,
        Ocean,
        Monochrome,
        NumPalettes
    };

    void setWaterfallPalette(WaterfallPalette palette);
    WaterfallPalette getWaterfallPalette() const { return currentPalette; }
    void cycleWaterfallPalette();

private:
    WaveSlaveAudioProcessor& processor;

    enum
    {
        fftOrder  = 11,
        fftSize   = 1 << fftOrder,
        scopeSize = 512
    };

    juce::dsp::FFT forwardFFT;
    juce::dsp::WindowingFunction<float> window;

    float fftData [2 * fftSize];
    float scopeData [scopeSize];
    void drawNextFrameOfSpectrum();

    // Interaction state
    int hoveredPartial = -1;
    int selectedPartial = 0;
    int draggedPartial = -1;
    juce::Point<float> dragStartMouse;
    float initialRatioNorm = 0.0f;
    float initialGainNorm = 0.0f;
    float initialSemitone = 0.0f;
    PartialKeyframe initialDragKeyframe;
    bool isFullCanvas = false;
    bool isReadOnly = false;

    bool isTimelineOpen = false;
    juce::Component* timelineHost = nullptr;

    // Interactive Keyframe Timeline state & controls
    float scrubTimeNorm = 0.0f;         // Current timeline playhead [0.0 .. 1.0]
    int selectedKeyframeIndex = -1;
    int draggedKeyframeIndex = -1;
    int draggedWaypointKeyframe = -1;
    bool isScrubbingTimeline = false;
    bool isSyncingTimelineControls = false;

    juce::ComboBox   timelinePartialBox;
    juce::TextButton timelinePrevBtn { "<" };
    juce::TextButton timelineNextBtn { ">" };
    void updateTimelinePartialBoxItems();

    juce::TextButton animEnableBtn { "OFF" };
    juce::TextButton addKfBtn      { "+KF" };
    juce::TextButton delKfBtn      { "-KF" };
    juce::TextButton clearKfBtn    { "CLR" };
    juce::ComboBox   animModeBox;
    juce::ComboBox   animCurveBox;
    juce::Slider     animDurSlider;

    // Visual modulation tracking (smooth interpolation & editor-side LFO/preview phase)
    std::array<float, 16> editorLfoPhases {};
    std::array<float, 16> previewAnimElapsed {};
    std::array<float, 16> previewAnimPhases {};
    std::array<float, 16> smoothedModRatio {};
    std::array<float, 16> smoothedModGain {};
    std::array<float, 16> smoothedModSemi {};
    std::array<float, 16> smoothedModDetune {};
    std::array<float, 16> smoothedModPan {};
    bool smoothedInit = false;
    juce::uint32 lastTimerMs = 0;

    struct ComputedPartialCoords
    {
        float anchorRatio = 1.0f, anchorGain = 0.0f, anchorSemi = 0.0f, anchorDetune = 0.0f, anchorPan = 0.0f;
        float modRatio = 1.0f, modGain = 0.0f, modSemi = 0.0f, modDetune = 0.0f, modPan = 0.0f;
        juce::Point<float> anchorPos;
        juce::Point<float> livePos;
    };

    juce::Rectangle<float> getGraphPlotBounds() const;
    juce::Rectangle<float> getTimelineTrackBounds() const;
    juce::Point<float> mapParamsToScreen(float effRatio, float gain, float semi, float detune,
                                         float left, float top, float width, float height, float baseFreq) const;
    ComputedPartialCoords computePartialCoords(int partialIdx, float left, float top, float width, float height, float baseFreq) const;
    int findKeyframeAtTimelinePos(float mouseX, float mouseY, juce::Rectangle<float> trackBounds) const;
    int findWaypointDiamondAt(float mouseX, float mouseY, int& outPartialIdx) const;
    float getEffectiveTimelineInfo(int partialIdx, int& outMode, float& outAtk, float& outDec, float& outRel) const;
    void repaintTimeline();

    int findPartialAt(float mouseX, float mouseY);
    int findClosestPartialX(float mouseX);
    float getEffectiveBaseFreq() const;
    SpectrumScaleMode scaleMode = SpectrumScaleMode::Sliding;
    juce::TextButton slideBtn     { "SLIDE" };
    juce::TextButton fullBtn      { "FULL" };
    juce::TextButton lockBtn      { "LOCK" };
    juce::TextButton showNodesBtn { "NODES" };
    bool isLocked = false;
    bool isWaterfallMode = false;

    // Waterfall state
    WaterfallPalette currentPalette = WaterfallPalette::Spectrum;
    std::array<juce::Colour, 1024> waterfallLut {};
    std::vector<float> waterfallHistory;

    juce::Image waterfallImage;
    int waterfallLineY = 0;        // current scroll row (top-down)
    float waterfallAccum = 0.0f;   // sub-frame accumulation for speed control

    struct WaterfallPartialDot
    {
        float currentX = 0.0f;
        float startX   = 0.0f;
        float targetX  = 0.0f;
        float freqHz   = 440.0f;
        bool  hasValidPos = false;
    };
    std::array<WaterfallPartialDot, 16> waterfallDots;
    int lastSeenTriggerCounter = -1;
    juce::uint32 dotAnimStartTimeMs = 0;
    bool isDotAnimRunning = false;
    float dotAnimAlpha = 0.0f;

    void buildWaterfallLut();
    void regenerateWaterfallImage();
    void updateWaterfallDots();
    void paintWaterfallDots(juce::Graphics& g, float left, float top, float width, float height);

    juce::Colour waterfallColour(float level) const;
    void paintWaterfall(juce::Graphics& g, float left, float top, float width, float height);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Spectrograph)
};

//==============================================================================
// Standalone Keyframe Animation Timeline Dock (positioned at bottom of screen)
class KeyframeTimeline : public juce::Component
{
public:
    explicit KeyframeTimeline(Spectrograph& spec) : spectrograph(spec)
    {
        spectrograph.setTimelineComponent(this);
    }

    ~KeyframeTimeline() override
    {
        spectrograph.setTimelineComponent(nullptr);
    }

    void paint(juce::Graphics& g) override
    {
        spectrograph.paintKeyframeTimeline(g, getLocalBounds().toFloat());
    }

    void resized() override
    {
        spectrograph.layoutKeyframeTimeline(getLocalBounds());
    }

    void mouseMove(const juce::MouseEvent& e) override        { spectrograph.handleTimelineMouseMove(e); }
    void mouseDown(const juce::MouseEvent& e) override        { spectrograph.handleTimelineMouseDown(e); }
    void mouseDrag(const juce::MouseEvent& e) override        { spectrograph.handleTimelineMouseDrag(e); }
    void mouseUp(const juce::MouseEvent& e) override          { spectrograph.handleTimelineMouseUp(e); }
    void mouseDoubleClick(const juce::MouseEvent& e) override { spectrograph.handleTimelineMouseDoubleClick(e); }

private:
    Spectrograph& spectrograph;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KeyframeTimeline)
};
