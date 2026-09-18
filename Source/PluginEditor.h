#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "Visualizer.h"

//==============================================================================
// A labeled knob helper component
class LabeledKnob : public juce::Component
{
public:
    LabeledKnob(const juce::String& labelText)
    {
        label.setText(labelText, juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centred);
        label.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.7f));
        label.setFont(juce::Font(11.0f));
        addAndMakeVisible(label);
        addAndMakeVisible(slider);
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 16);
        slider.setScrollWheelEnabled(false);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        label.setBounds(area.removeFromTop(16));
        slider.setBounds(area);
    }

    juce::Slider slider;
    juce::Label label;
};

class LabeledSlider : public juce::Component
{
public:
    LabeledSlider(const juce::String& labelText, bool isVertical = true)
        : vertical(isVertical)
    {
        label.setText(labelText, juce::dontSendNotification);
        label.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.7f));
        label.setFont(juce::Font(11.0f));
        addAndMakeVisible(label);
        addAndMakeVisible(slider);
        slider.setScrollWheelEnabled(false);
        
        if (vertical)
        {
            label.setJustificationType(juce::Justification::centred);
            slider.setSliderStyle(juce::Slider::LinearVertical);
            slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 40, 16);
        }
        else
        {
            label.setJustificationType(juce::Justification::centredLeft);
            slider.setSliderStyle(juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 40, 16);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds();
        if (vertical)
        {
            label.setBounds(area.removeFromTop(16));
            slider.setBounds(area);
        }
        else
        {
            label.setBounds(area.removeFromLeft(50)); // fixed width for label
            slider.setBounds(area);
        }
    }

    juce::Slider slider;
    juce::Label label;
    bool vertical;
};

//==============================================================================
// A single partial strip component (self-contained card)
//==============================================================================


//==============================================================================
// A single partial strip component (self-contained card with drag pad)
class PartialStrip : public juce::Component
{
public:
    PartialStrip(int index, juce::AudioProcessorValueTreeState& apvts)
        : partialIndex(index)
    {
        juce::String idSuffix = juce::String(index + 1);

        headerLabel.setText("H" + idSuffix, juce::dontSendNotification);
        headerLabel.setJustificationType(juce::Justification::centred);
        headerLabel.setFont(juce::Font(13.0f, juce::Font::bold));
        headerLabel.setColour(juce::Label::textColourId, getPartialColour(index));
        addAndMakeVisible(headerLabel);

        addAndMakeVisible(waveformBox);
        waveformBox.addItemList({ "Sine", "Triangle", "Saw", "Square", "Pulse", "Sharkfin", "Golden Spiral", "Noise", "Custom" }, 1);
        waveformAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(apvts, "WAVEFORM" + idSuffix, waveformBox);

        addAndMakeVisible(ratioSlider);
        ratioSlider.slider.setDoubleClickReturnValue(true, (double)(index + 1));
        ratioAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "RATIO" + idSuffix, ratioSlider.slider);

        addAndMakeVisible(semitoneSlider);
        semitoneSlider.slider.setDoubleClickReturnValue(true, 0.0);
        semitoneAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "SEMITONE" + idSuffix, semitoneSlider.slider);

        addAndMakeVisible(detuneSlider);
        detuneSlider.slider.setDoubleClickReturnValue(true, 0.0);
        detuneAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "DETUNE" + idSuffix, detuneSlider.slider);

        addAndMakeVisible(gainSlider);
        gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "GAIN" + idSuffix, gainSlider.slider);

        modLabel.setText("MODULATION", juce::dontSendNotification);
        modLabel.setJustificationType(juce::Justification::centred);
        modLabel.setFont(juce::Font(11.0f, juce::Font::bold));
        modLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF888888));
        addAndMakeVisible(modLabel);

        addAndMakeVisible(modTargetBox);
        modTargetBox.addItemList({ "None", "Pitch", "Level" }, 1);
        modTargetAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(apvts, "MOD_TARGET" + idSuffix, modTargetBox);

        addAndMakeVisible(modWaveBox);
        modWaveBox.addItemList({ "Sine", "Triangle", "Saw", "Square" }, 1);
        modWaveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(apvts, "MOD_WAVEFORM" + idSuffix, modWaveBox);

        addAndMakeVisible(modRateSlider);
        modRateSlider.slider.setDoubleClickReturnValue(true, 2.0);
        modRateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "MOD_RATE" + idSuffix, modRateSlider.slider);

        addAndMakeVisible(modDepthSlider);
        modDepthSlider.slider.setDoubleClickReturnValue(true, 0.0);
        modDepthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "MOD_DEPTH" + idSuffix, modDepthSlider.slider);

        addAndMakeVisible(modPhaseSlider);
        modPhaseSlider.slider.setDoubleClickReturnValue(true, 0.0);
        modPhaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "MOD_PHASE" + idSuffix, modPhaseSlider.slider);
    }

    void setSelected(bool selected)
    {
        if (isSelected != selected)
        {
            isSelected = selected;
            repaint();
        }
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced(2.0f);
        auto col = getPartialColour(partialIndex);

        g.setColour(isSelected ? juce::Colour(0xFF252525) : juce::Colour(0xFF151515));
        g.fillRoundedRectangle(bounds, 6.0f);

        if (isSelected)
        {
            g.setColour(col);
            g.drawRoundedRectangle(bounds, 6.0f, 2.0f);
        }
        else
        {
            g.setColour(juce::Colour(0xFF333333));
            g.drawRoundedRectangle(bounds, 6.0f, 1.0f);
        }

        // Top accent bar matching partial color
        g.setColour(col);
        g.fillRoundedRectangle(bounds.getX() + 6.0f, bounds.getY() + 3.0f, bounds.getWidth() - 12.0f, 3.0f, 1.5f);
    }

    void mouseDown(const juce::MouseEvent&) override
    {
        if (onSelected) onSelected(partialIndex);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8);

        // Header: "H1"
        headerLabel.setBounds(area.removeFromTop(20));
        area.removeFromTop(4);

        // Row 1: Waveform selector
        waveformBox.setBounds(area.removeFromTop(24));
        area.removeFromTop(6);

        // Stacked horizontal sliders
        int sliderH = 22;
        ratioSlider.setBounds(area.removeFromTop(sliderH));
        area.removeFromTop(4);
        semitoneSlider.setBounds(area.removeFromTop(sliderH));
        area.removeFromTop(4);
        detuneSlider.setBounds(area.removeFromTop(sliderH));
        area.removeFromTop(4);
        gainSlider.setBounds(area.removeFromTop(sliderH));
        area.removeFromTop(10); // Spacing before mod section

        // Modulation
        modLabel.setBounds(area.removeFromTop(16));
        area.removeFromTop(4);

        modTargetBox.setBounds(area.removeFromTop(24));
        area.removeFromTop(4);
        modWaveBox.setBounds(area.removeFromTop(24));
        area.removeFromTop(6);
        modRateSlider.setBounds(area.removeFromTop(sliderH));
        area.removeFromTop(4);
        modDepthSlider.setBounds(area.removeFromTop(sliderH));
        area.removeFromTop(4);
        modPhaseSlider.setBounds(area.removeFromTop(sliderH));
    }

    std::function<void(int)> onSelected;

private:
    int partialIndex;
    bool isSelected = false;

    juce::Label headerLabel;
    juce::ComboBox waveformBox;
    LabeledSlider ratioSlider { "Ratio", false };
    LabeledSlider semitoneSlider { "Semi", false };
    LabeledSlider detuneSlider { "Detune", false };
    LabeledSlider gainSlider { "Level", false };
    
    juce::ComboBox modTargetBox;
    juce::ComboBox modWaveBox;
    LabeledSlider modRateSlider { "Mod Rate", false };
    LabeledSlider modDepthSlider { "Mod Depth", false };
    LabeledSlider modPhaseSlider { "Mod Phase", false };
    juce::Label modLabel;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> waveformAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> ratioAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> semitoneAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> detuneAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modTargetAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modWaveAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> modRateAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> modDepthAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> modPhaseAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PartialStrip)
};

//==============================================================================
// Container that lays out active PartialStrip cards vertically in the right-side pane
class PartialsContainer : public juce::Component
{
public:
    static constexpr int stripCardHeight = 380;

    void initPartials(juce::AudioProcessorValueTreeState& apvts, int count, std::function<void(int)> onSelect)
    {
        for (int i = 0; i < count; ++i)
        {
            auto* strip = new PartialStrip(i, apvts);
            strip->onSelected = onSelect;
            addAndMakeVisible(strip);
            strips.add(strip);
        }
        activeCount = count;
    }

    void setActiveCount(int count)
    {
        activeCount = juce::jlimit(0, strips.size(), count);
        for (int i = 0; i < strips.size(); ++i)
            strips[i]->setVisible(i < activeCount);

        setSize(getWidth(), stripCardHeight * activeCount);
        resized();
    }

    int getActiveCount() const { return activeCount; }

    void setSelectedPartial(int index, juce::Viewport* viewport = nullptr)
    {
        for (int i = 0; i < strips.size(); ++i)
            strips[i]->setSelected(i == index);

        if (viewport != nullptr && index >= 0 && index < activeCount)
        {
            int targetY = index * stripCardHeight;
            int viewY = juce::jlimit(0, juce::jmax(0, getHeight() - viewport->getHeight()),
                                     targetY - viewport->getHeight() / 2 + stripCardHeight / 2);
            viewport->setViewPosition(0, viewY);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds();
        for (int i = 0; i < activeCount && i < strips.size(); ++i)
            strips[i]->setBounds(area.removeFromTop(stripCardHeight).reduced(0, 3));
    }

private:
    juce::OwnedArray<PartialStrip> strips;
    int activeCount = 16;
};

//==============================================================================
class WaveSlaveAudioProcessorEditor : public juce::AudioProcessorEditor, public juce::ValueTree::Listener
{
public:
    enum class VisualizerMode { Dual, HarmonicsOnly, ScopeOnly, SpectroscopeOnly, PolarScope };

    WaveSlaveAudioProcessorEditor (WaveSlaveAudioProcessor&);
    ~WaveSlaveAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;



    void setNumPartials(int newCount);
    void applyBulkWaveform(int waveTypeIndex);
    void applyBulkHarmonicSeries(int seriesType);
    void applyBulkGainTilt(int tiltType);
    void applyBulkDetune(int spreadType);
    void applyBulkSemitones(int semitoneValue);
    void applyBulkModTarget(int targetIndex);
    void applyBulkModDepth(float depthValue);
    void resetAllPartials();
    void applyFactoryPreset(int index);

    void valueTreePropertyChanged(juce::ValueTree& treeWhosePropertyHasChanged, const juce::Identifier& property) override;
    void valueTreeChildAdded(juce::ValueTree& parentTree, juce::ValueTree& childWhichHasBeenAdded) override {}
    void valueTreeChildRemoved(juce::ValueTree& parentTree, juce::ValueTree& childWhichHasBeenRemoved, int indexFromWhichChildWasRemoved) override {}
    void valueTreeChildOrderChanged(juce::ValueTree& parentTreeWhoseChildrenHaveMoved, int oldIndex, int newIndex) override {}
    void valueTreeParentChanged(juce::ValueTree& treeWhoseParentHasChanged) override {}

private:
    WaveSlaveAudioProcessor& audioProcessor;
    WaveSlaveLookAndFeel customLookAndFeel;

    juce::ComboBox fileMenuBox;
    std::unique_ptr<juce::FileChooser> fileChooser;
    bool isApplyingPreset = false;

    Oscilloscope oscilloscope;
    Spectrograph spectrograph;

    // Master Gain horizontal slider
    LabeledSlider masterGainSlider { "Master", false };

    // Factory Presets
    juce::ComboBox factoryPresetBox;
    juce::ComboBox partialsComboBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> partialsAttachment;

    // Bulk Edit controls
    juce::ComboBox bulkEditBox;

    // ADSR
    LabeledSlider attackVSlider  { "Attack", false };
    LabeledSlider decayVSlider   { "Decay", false };
    LabeledSlider sustainVSlider { "Sustain", false };
    LabeledSlider releaseVSlider { "Release", false };

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> masterGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> numPartialsAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attackAttachment, decayAttachment, sustainAttachment, releaseAttachment;

    // Partials
    juce::Viewport partialsViewport;
    PartialsContainer partialsContainer;

    // Base Note controls
    juce::Label baseNoteLabel;
    juce::ComboBox baseNoteBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> baseNoteAttachment;

    // Base Waveform controls
    juce::Label baseWaveformLabel;
    juce::ComboBox baseWaveformBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> baseWaveformAttachment;

    // Section labels
    juce::Label titleLabel, envelopeLabel, partialsLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveSlaveAudioProcessorEditor)
};
