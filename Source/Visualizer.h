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
        else
        {
            LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        }
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

    void clearWavetable();

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    WaveSlaveAudioProcessor& processor;
    juce::AudioBuffer<float> displayBuffer;
    juce::AudioBuffer<float> partialDisplayBuffer;
    bool showOverlays = true;
    bool isPolar = false;
    bool isDrawMode = false;
    juce::TextButton overlayBtn { "PARTIALS" };
    juce::TextButton polarBtn { "POLAR" };
    juce::TextButton drawBtn { "DRAW" };
    juce::ToggleButton bezierToggle { "Bezier" };

    std::vector<juce::Point<float>> splinePoints;
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
    void setSelectedPartial(int index) { selectedPartial = index; repaint(); }
    int getSelectedPartial() const { return selectedPartial; }

    void setFullCanvasMode(bool full) { isFullCanvas = full; repaint(); }
    bool isFullCanvasMode() const { return isFullCanvas; }

    void setReadOnly(bool readOnly) { isReadOnly = readOnly; }
    bool isReadOnlyMode() const { return isReadOnly; }

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
    float initialDetune = 0.0f;
    float initialSemitone = 0.0f;
    bool isFullCanvas = false;
    bool isReadOnly = false;

    int findPartialAt(float mouseX, float mouseY);
    int findClosestPartialX(float mouseX);
    juce::TextButton showNodesBtn { "SHOW NODES" };
    juce::TextButton staticModeBtn { "STATIC X-AXIS" };
    juce::TextButton lockNodesBtn { "LOCK NOTES" };
    bool isStaticMode = false;
    bool isNodesLocked = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Spectrograph)
};
