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
    LabeledSlider(const juce::String& labelText, bool isVertical = true, int customLabelWidth = -1)
        : vertical(isVertical), labelWidth(customLabelWidth)
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
            slider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 36, 16);
        }
    }

    void setLabelWidth(int w) { labelWidth = w; resized(); }

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
            int lw = (labelWidth > 0) ? labelWidth : 50;
            label.setBounds(area.removeFromLeft(lw));
            slider.setBounds(area);
        }
    }

    juce::Slider slider;
    juce::Label label;
    bool vertical;
    int labelWidth = -1;
};

//==============================================================================
// A single partial strip component (self-contained card)
//==============================================================================


//==============================================================================
// A single partial strip component (self-contained card with drag pad)
class PartialStrip : public juce::Component
{
public:
    static constexpr int expandedCardHeight = 630;
    static constexpr int minimizedCardHeight = 82;

    PartialStrip(int index, juce::AudioProcessorValueTreeState& apvts)
        : partialIndex(index)
    {
        juce::String idSuffix = juce::String(index + 1);

        if (index == 0)
        {
            headerLabel.setText("BASE NOTE", juce::dontSendNotification);
            headerLabel.setFont(juce::Font(12.0f, juce::Font::bold));
            headerLabel.setColour(juce::Label::textColourId, getPartialColour(0));
        }
        else
        {
            headerLabel.setText("H" + idSuffix, juce::dontSendNotification);
            headerLabel.setFont(juce::Font(13.0f, juce::Font::bold));
            headerLabel.setColour(juce::Label::textColourId, getPartialColour(index));
        }
        headerLabel.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(headerLabel);

        minButton.setButtonText("-");
        minButton.setTooltip("Minimize / Expand Partial");
        minButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0x00000000));
        minButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white.withAlpha(0.6f));
        minButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        addAndMakeVisible(minButton);
        minButton.onClick = [this]()
        {
            setMinimized(!isMinimized);
        };

        enableButton.setButtonText("ON");
        enableButton.setClickingTogglesState(true);
        enableButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF222222));
        enableButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFF444444));
        enableButton.setColour(juce::TextButton::textColourOnId, juce::Colour(0xFFFFFFFF));
        enableButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xFF666666));
        enableButton.setTooltip("Enable / Disable Partial");
        addAndMakeVisible(enableButton);
        enableAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(apvts, "P_ENABLED" + idSuffix, enableButton);
        enableButton.onClick = [this]()
        {
            bool on = enableButton.getToggleState();
            enableButton.setButtonText(on ? "ON" : "OFF");
            updateChildVisibility();
            resized();
            repaint();
            if (onLayoutChanged) onLayoutChanged();
        };
        enableButton.setButtonText(enableButton.getToggleState() ? "ON" : "OFF");

        addAndMakeVisible(waveformBox);
        waveformBox.addItemList({ "Sine", "Triangle", "Saw", "Square", "Pulse", "Sharkfin", "Fibonacci", "Noise", "Custom 1", "Custom 2", "Custom 3", "Custom 4" }, 1);
        waveformAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(apvts, "WAVEFORM" + idSuffix, waveformBox);

        addAndMakeVisible(ratioSlider);
        ratioSlider.slider.setDoubleClickReturnValue(true, (double)(index + 1));
        ratioAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "RATIO" + idSuffix, ratioSlider.slider);
        ratioSlider.slider.onValueChange = [this]()
        {
            if (ratioSlider.slider.isMouseButtonDown() && onParamEditedByUser)
                onParamEditedByUser(partialIndex);
        };

        addAndMakeVisible(semitoneSlider);
        semitoneSlider.slider.setDoubleClickReturnValue(true, 0.0);
        semitoneSlider.slider.setTooltip("Coarse Semitone Tuning (-24 to +24 semitones)");
        semitoneAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "SEMITONE" + idSuffix, semitoneSlider.slider);
        semitoneSlider.slider.onValueChange = [this]()
        {
            if (semitoneSlider.slider.isMouseButtonDown() && onParamEditedByUser)
                onParamEditedByUser(partialIndex);
        };

        addAndMakeVisible(gainSlider);
        gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "GAIN" + idSuffix, gainSlider.slider);
        gainSlider.slider.onValueChange = [this]()
        {
            if (gainSlider.slider.isMouseButtonDown() && onParamEditedByUser)
                onParamEditedByUser(partialIndex);
        };

        addAndMakeVisible(panSlider);
        panSlider.slider.setDoubleClickReturnValue(true, 0.0);
        panAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "PAN" + idSuffix, panSlider.slider);
        panSlider.slider.onValueChange = [this]()
        {
            if (panSlider.slider.isMouseButtonDown() && onParamEditedByUser)
                onParamEditedByUser(partialIndex);
        };

        // --- Per-Partial ADSR Envelope ---
        envLabel.setText("ADSR", juce::dontSendNotification);
        envLabel.setJustificationType(juce::Justification::centredLeft);
        envLabel.setFont(juce::Font(10.0f, juce::Font::bold));
        envLabel.setColour(juce::Label::textColourId, juce::Colour(0xFFFFFFFF));
        addAndMakeVisible(envLabel);

        pAdsrEnableBtn.setClickingTogglesState(true);
        pAdsrEnableBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFF444444));
        pAdsrEnableBtn.setColour(juce::TextButton::textColourOnId, juce::Colour(0xFFFFFFFF));
        pAdsrEnableBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xFF666666));
        pAdsrEnableBtn.setTooltip("Enable / Disable Per-Partial ADSR");
        addAndMakeVisible(pAdsrEnableBtn);
        pAdsrEnableAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(apvts, "P_ADSR_ENABLED" + idSuffix, pAdsrEnableBtn);
        pAdsrEnableBtn.onClick = [this]()
        {
            pAdsrEnableBtn.setButtonText(pAdsrEnableBtn.getToggleState() ? "ON" : "OFF");
            updateChildVisibility();
            resized();
            repaint();
            if (onLayoutChanged) onLayoutChanged();
        };
        pAdsrEnableBtn.setButtonText(pAdsrEnableBtn.getToggleState() ? "ON" : "OFF");

        addAndMakeVisible(pAttackSlider);
        pAttackSlider.slider.setDoubleClickReturnValue(true, 0.01);
        pAttackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "P_ATTACK" + idSuffix, pAttackSlider.slider);

        addAndMakeVisible(pDecaySlider);
        pDecaySlider.slider.setDoubleClickReturnValue(true, 0.3);
        pDecayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "P_DECAY" + idSuffix, pDecaySlider.slider);

        addAndMakeVisible(pSustainSlider);
        pSustainSlider.slider.setDoubleClickReturnValue(true, 1.0);
        pSustainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "P_SUSTAIN" + idSuffix, pSustainSlider.slider);

        addAndMakeVisible(pReleaseSlider);
        pReleaseSlider.slider.setDoubleClickReturnValue(true, 0.5);
        pReleaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "P_RELEASE" + idSuffix, pReleaseSlider.slider);

        addAndMakeVisible(pGlideSlider);
        pGlideSlider.slider.setDoubleClickReturnValue(true, 0.0);
        pGlideAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "P_GLIDE" + idSuffix, pGlideSlider.slider);

        // --- LFO Modulation ---
        modLabel.setText("LFO", juce::dontSendNotification);
        modLabel.setJustificationType(juce::Justification::centredLeft);
        modLabel.setFont(juce::Font(10.0f, juce::Font::bold));
        modLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF888888));
        addAndMakeVisible(modLabel);

        pLfoEnableBtn.setClickingTogglesState(true);
        pLfoEnableBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFF444444));
        pLfoEnableBtn.setColour(juce::TextButton::textColourOnId, juce::Colour(0xFFFFFFFF));
        pLfoEnableBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xFF666666));
        pLfoEnableBtn.setTooltip("Enable / Disable LFO");
        addAndMakeVisible(pLfoEnableBtn);
        pLfoEnableBtn.onClick = [this]()
        {
            bool turningOn = pLfoEnableBtn.getToggleState();
            pLfoEnableBtn.setButtonText(turningOn ? "ON" : "OFF");
            if (turningOn)
            {
                if (modTargetBox.getSelectedId() <= 1)
                    modTargetBox.setSelectedId(2, juce::sendNotificationSync);
            }
            else
            {
                modTargetBox.setSelectedId(1, juce::sendNotificationSync);
            }
            updateChildVisibility();
            resized();
            repaint();
            if (onLayoutChanged) onLayoutChanged();
        };

        addAndMakeVisible(modTargetBox);
        modTargetBox.addItemList({ "None", "Pitch", "Level", "Pan", "Detune" }, 1);
        modTargetAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(apvts, "MOD_TARGET" + idSuffix, modTargetBox);
        modTargetBox.onChange = [this]()
        {
            bool on = modTargetBox.getSelectedId() > 1;
            pLfoEnableBtn.setToggleState(on, juce::dontSendNotification);
            pLfoEnableBtn.setButtonText(on ? "ON" : "OFF");
            updateChildVisibility();
            resized();
            repaint();
            if (onLayoutChanged) onLayoutChanged();
        };
        pLfoEnableBtn.setToggleState(modTargetBox.getSelectedId() > 1, juce::dontSendNotification);
        pLfoEnableBtn.setButtonText(pLfoEnableBtn.getToggleState() ? "ON" : "OFF");

        addAndMakeVisible(modWaveBox);
        modWaveBox.addItemList({ "Sine", "Triangle", "Saw", "Square", "Custom 1", "Custom 2", "Custom 3", "Custom 4" }, 1);
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

        // --- Keyframe Animation (replacing Mod 2) ---
        dynModLabel.setText("KEYFRAME", juce::dontSendNotification);
        dynModLabel.setJustificationType(juce::Justification::centredLeft);
        dynModLabel.setFont(juce::Font(10.0f, juce::Font::bold));
        dynModLabel.setColour(juce::Label::textColourId, juce::Colour(0xFFFFFFFF));
        addAndMakeVisible(dynModLabel);

        pDynModEnableBtn.setClickingTogglesState(true);
        pDynModEnableBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFF444444));
        pDynModEnableBtn.setColour(juce::TextButton::textColourOnId, juce::Colour(0xFFFFFFFF));
        pDynModEnableBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xFF666666));
        pDynModEnableBtn.setTooltip("Enable / Disable Keyframe Animation");
        addAndMakeVisible(pDynModEnableBtn);
        pDynModEnableBtn.onClick = [this]()
        {
            bool turningOn = pDynModEnableBtn.getToggleState();
            pDynModEnableBtn.setButtonText(turningOn ? "ON" : "OFF");
            if (turningOn)
            {
                if (dynModModeBox.getSelectedId() <= 1)
                    dynModModeBox.setSelectedId(2, juce::sendNotificationSync); // Default to Note Length
                if (onEnsureKeyframes) onEnsureKeyframes(partialIndex);
            }
            else
            {
                dynModModeBox.setSelectedId(1, juce::sendNotificationSync);
                if (onEnsureKeyframes) onEnsureKeyframes(partialIndex);
            }
            updateChildVisibility();
            resized();
            repaint();
            if (onLayoutChanged) onLayoutChanged();
        };

        addChildComponent(dynModModeBox);
        dynModModeBox.addItemList({ "Off", "Note Length", "Time: One-Shot", "Time: Loop", "Time: Ping-Pong" }, 1);
        dynModModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(apvts, "ANIM_MODE" + idSuffix, dynModModeBox);
        dynModModeBox.onChange = [this]()
        {
            bool on = dynModModeBox.getSelectedId() > 1;
            pDynModEnableBtn.setToggleState(on, juce::dontSendNotification);
            pDynModEnableBtn.setButtonText(on ? "ON" : "OFF");
            if (onEnsureKeyframes)
                onEnsureKeyframes(partialIndex);
            updateChildVisibility();
            resized();
            repaint();
            if (onLayoutChanged) onLayoutChanged();
        };
        pDynModEnableBtn.setToggleState(dynModModeBox.getSelectedId() > 1, juce::dontSendNotification);
        pDynModEnableBtn.setButtonText(pDynModEnableBtn.getToggleState() ? "ON" : "OFF");

        addChildComponent(dynModTargetBox);
        dynModTargetBox.addItemList({ "Smooth", "Linear", "Step" }, 1);
        dynModTargetAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(apvts, "ANIM_CURVE" + idSuffix, dynModTargetBox);

        addChildComponent(dynModRateSlider);
        dynModRateSlider.slider.setDoubleClickReturnValue(true, 2.0);
        dynModRateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "ANIM_DURATION" + idSuffix, dynModRateSlider.slider);

        addChildComponent(dynModDepthSlider);
        dynModDepthSlider.slider.setDoubleClickReturnValue(true, 1.0);
        dynModDepthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "ANIM_DEPTH" + idSuffix, dynModDepthSlider.slider);

        updateChildVisibility();
    }

    void setMinimized(bool min)
    {
        if (isMinimized != min)
        {
            isMinimized = min;
            minButton.setButtonText(isMinimized ? "+" : "-");
            updateChildVisibility();
            resized();
            repaint();
            if (onLayoutChanged) onLayoutChanged();
        }
    }

    bool getIsMinimized() const { return isMinimized; }

    void setSections(bool adsr, bool lfo, bool dynMod)
    {
        showAdsr = adsr;
        showLfo = lfo;
        showDynMod = dynMod;
        updateChildVisibility();
        resized();
        repaint();
    }

    int getDesiredHeight() const
    {
        if (!enableButton.getToggleState()) return 32; // Compact bar when partial is disabled
        if (isMinimized) return minimizedCardHeight;   // 82px when collapsed

        int h = 160; // Header (20) + gap (4) + waveform (24) + gap (6) + 4 slider rows (4*22 + 4*4 = 104) + padding
        if (showAdsr)
        {
            if (pAdsrEnableBtn.getToggleState())
                h += 160; // Header (16) + gap (4) + 5 sliders (5*22 + 4*4 = 126) + gap (14) = 160
            else
                h += 24; // Just ADSR header (sub-settings hidden)
        }

        if (showLfo)
        {
            if (modTargetBox.getSelectedId() > 1)
                h += 166;
            else
                h += 48; // Just LFO header + Target selector (sub-settings hidden)
        }

        if (showDynMod)
        {
            h += 24; // Compact row for KEYFRAME label and ON/OFF button (no dropdowns)
        }

        return h;
    }

    void updateChildVisibility()
    {
        bool enabled = enableButton.getToggleState();
        bool full = enabled && !isMinimized;

        waveformBox.setVisible(enabled);
        ratioSlider.setVisible(full);
        gainSlider.setVisible(enabled);
        semitoneSlider.setVisible(full);
        panSlider.setVisible(full);

        bool adsrSecVis = full && showAdsr;
        bool adsrSubVis = adsrSecVis && pAdsrEnableBtn.getToggleState();
        envLabel.setVisible(adsrSecVis);
        pAdsrEnableBtn.setVisible(adsrSecVis);
        pAttackSlider.setVisible(adsrSubVis);
        pDecaySlider.setVisible(adsrSubVis);
        pSustainSlider.setVisible(adsrSubVis);
        pReleaseSlider.setVisible(adsrSubVis);
        pGlideSlider.setVisible(adsrSubVis);

        bool lfoSecVis = full && showLfo;
        bool lfoSubVis = lfoSecVis && (modTargetBox.getSelectedId() > 1);
        modLabel.setVisible(lfoSecVis);
        pLfoEnableBtn.setVisible(lfoSecVis);
        modTargetBox.setVisible(lfoSecVis);
        modWaveBox.setVisible(lfoSubVis);
        modRateSlider.setVisible(lfoSubVis);
        modDepthSlider.setVisible(lfoSubVis);
        modPhaseSlider.setVisible(lfoSubVis);

        bool dynSecVis = full && showDynMod;
        dynModLabel.setVisible(dynSecVis);
        pDynModEnableBtn.setVisible(dynSecVis);
        dynModModeBox.setVisible(false);
        dynModTargetBox.setVisible(false);
        dynModRateSlider.setVisible(false);
        dynModDepthSlider.setVisible(false);
    }

    void setSelected(bool selected)
    {
        if (isSelected != selected)
        {
            isSelected = selected;
            repaint();
        }
    }

    void refreshState()
    {
        bool enabled = enableButton.getToggleState();
        enableButton.setButtonText(enabled ? "ON" : "OFF");

        bool adsrOn = pAdsrEnableBtn.getToggleState();
        pAdsrEnableBtn.setButtonText(adsrOn ? "ON" : "OFF");

        bool lfoOn = modTargetBox.getSelectedId() > 1;
        pLfoEnableBtn.setToggleState(lfoOn, juce::dontSendNotification);
        pLfoEnableBtn.setButtonText(lfoOn ? "ON" : "OFF");

        bool dynOn = dynModModeBox.getSelectedId() > 1;
        pDynModEnableBtn.setToggleState(dynOn, juce::dontSendNotification);
        pDynModEnableBtn.setButtonText(dynOn ? "ON" : "OFF");

        updateChildVisibility();
        resized();
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced(2.0f);
        auto col = getPartialColour(partialIndex);
        bool enabled = enableButton.getToggleState();

        if (!enabled)
        {
            g.setColour(juce::Colour(0xFF101010));
            g.fillRoundedRectangle(bounds, 6.0f);
            g.setColour(juce::Colour(0xFF242424));
            g.drawRoundedRectangle(bounds, 6.0f, 1.0f);

            // Dimmed top accent bar
            g.setColour(col.withAlpha(0.25f));
            g.fillRoundedRectangle(bounds.getX() + 6.0f, bounds.getY() + 3.0f, bounds.getWidth() - 12.0f, 2.0f, 1.0f);
            return;
        }

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
        auto area = getLocalBounds().reduced(6);

        // Header: "H1", enable button ("ON"/"OFF"), and minimize button ("-"/"+")
        auto headerRow = area.removeFromTop(20);
        minButton.setBounds(headerRow.removeFromRight(18));
        headerRow.removeFromRight(4);
        enableButton.setBounds(headerRow.removeFromRight(36));
        headerRow.removeFromRight(4);
        headerLabel.setBounds(headerRow);
        area.removeFromTop(4);

        if (!enableButton.getToggleState())
            return;

        if (isMinimized)
        {
            waveformBox.setBounds(area.removeFromTop(22));
            area.removeFromTop(4);
            gainSlider.setBounds(area.removeFromTop(22));
            return;
        }

        // Row 1: Waveform selector
        waveformBox.setBounds(area.removeFromTop(24));
        area.removeFromTop(6);

        // Stacked horizontal sliders: Ratio, Level (below Ratio), Consolidated Tune (Semi & Fine), Pan
        int sliderH = 22;
        ratioSlider.setBounds(area.removeFromTop(sliderH));
        area.removeFromTop(4);

        gainSlider.setBounds(area.removeFromTop(sliderH));
        area.removeFromTop(4);

        semitoneSlider.setBounds(area.removeFromTop(sliderH));
        area.removeFromTop(4);

        panSlider.setBounds(area.removeFromTop(sliderH));
        area.removeFromTop(4);

        if (showAdsr)
        {
            // Per-Partial ADSR Envelope
            auto adsrHeader = area.removeFromTop(16);
            pAdsrEnableBtn.setBounds(adsrHeader.removeFromRight(32).reduced(0, 1));
            adsrHeader.removeFromRight(4);
            envLabel.setBounds(adsrHeader);
            area.removeFromTop(4);

            if (pAdsrEnableBtn.getToggleState())
            {
                pAttackSlider.setBounds(area.removeFromTop(sliderH));
                area.removeFromTop(4);
                pDecaySlider.setBounds(area.removeFromTop(sliderH));
                area.removeFromTop(4);
                pSustainSlider.setBounds(area.removeFromTop(sliderH));
                area.removeFromTop(4);
                pReleaseSlider.setBounds(area.removeFromTop(sliderH));
                area.removeFromTop(4);
                pGlideSlider.setBounds(area.removeFromTop(sliderH));
                area.removeFromTop(8);
            }
            else
            {
                area.removeFromTop(4);
            }
        }

        if (showLfo)
        {
            // LFO Modulation
            auto lfoHeader = area.removeFromTop(16);
            pLfoEnableBtn.setBounds(lfoHeader.removeFromRight(32).reduced(0, 1));
            lfoHeader.removeFromRight(4);
            modLabel.setBounds(lfoHeader);
            area.removeFromTop(4);
            modTargetBox.setBounds(area.removeFromTop(24));
            area.removeFromTop(4);

            if (modTargetBox.getSelectedId() > 1)
            {
                modWaveBox.setBounds(area.removeFromTop(24));
                area.removeFromTop(6);
                modRateSlider.setBounds(area.removeFromTop(sliderH));
                area.removeFromTop(4);
                modDepthSlider.setBounds(area.removeFromTop(sliderH));
                area.removeFromTop(4);
                modPhaseSlider.setBounds(area.removeFromTop(sliderH));
                area.removeFromTop(8);
            }
            else
            {
                area.removeFromTop(4);
            }
        }

        if (showDynMod)
        {
            // Keyframe Animation: Compact header with ON/OFF button (no dropdowns)
            auto dynHeader = area.removeFromTop(18);
            pDynModEnableBtn.setBounds(dynHeader.removeFromRight(34).reduced(0, 1));
            dynHeader.removeFromRight(4);
            dynModLabel.setBounds(dynHeader);
            area.removeFromTop(4);
        }
    }

    std::function<void(int)> onSelected;
    std::function<void()> onLayoutChanged;
    std::function<void(int)> onParamEditedByUser;
    std::function<void(int)> onEnsureKeyframes;

private:
    int partialIndex;
    bool isSelected = false;
    bool isMinimized = false;
    bool showAdsr = true;
    bool showLfo = false;
    bool showDynMod = false;

    juce::Label headerLabel;
    juce::TextButton enableButton;
    juce::TextButton minButton;
    juce::ComboBox waveformBox;
    LabeledSlider ratioSlider    { "Ratio", false, 44 };
    LabeledSlider gainSlider     { "Level", false, 44 };
    LabeledSlider semitoneSlider { "Semi",  false, 44 };
    LabeledSlider panSlider      { "Pan",   false, 44 };

    // Per-Partial ADSR
    juce::Label envLabel;
    juce::TextButton pAdsrEnableBtn;
    LabeledSlider pAttackSlider  { "Atk", false };
    LabeledSlider pDecaySlider   { "Dec", false };
    LabeledSlider pSustainSlider { "Sus", false };
    LabeledSlider pReleaseSlider { "Rel", false };
    LabeledSlider pGlideSlider   { "Glide", false };
    
    // LFO Modulation
    juce::Label modLabel;
    juce::TextButton pLfoEnableBtn;
    juce::ComboBox modTargetBox;
    juce::ComboBox modWaveBox;
    LabeledSlider modRateSlider  { "Rate",  false };
    LabeledSlider modDepthSlider { "Depth", false };
    LabeledSlider modPhaseSlider { "Phase", false };

    // Keyframe Animation
    juce::Label dynModLabel;
    juce::TextButton pDynModEnableBtn;
    juce::ComboBox dynModModeBox;
    juce::ComboBox dynModTargetBox;
    LabeledSlider dynModRateSlider  { "Dur", false };
    LabeledSlider dynModDepthSlider { "Depth", false };

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> enableAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> waveformAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> ratioAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> semitoneAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> panAttachment;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> pAdsrEnableAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> pAttackAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> pDecayAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> pSustainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> pReleaseAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> pGlideAttachment;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modTargetAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modWaveAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> modRateAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> modDepthAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> modPhaseAttachment;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> dynModModeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> dynModTargetAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dynModRateAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dynModDepthAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PartialStrip)
};

//==============================================================================
// Container that lays out active PartialStrip cards in a responsive grid
class PartialsContainer : public juce::Component
{
public:
    static constexpr int stripCardHeight = 380;
    static constexpr int stripCardMinHeight = 82;

    void initPartials(juce::AudioProcessorValueTreeState& apvts, int count, std::function<void(int)> onSelect)
    {
        for (int i = 0; i < count; ++i)
        {
            auto* strip = new PartialStrip(i, apvts);
            strip->onSelected = onSelect;
            strip->onParamEditedByUser = [this](int idx)
            {
                if (onParamEditedByUser) onParamEditedByUser(idx);
            };
            strip->onEnsureKeyframes = [this](int idx)
            {
                if (onEnsureKeyframes) onEnsureKeyframes(idx);
            };
            strip->onLayoutChanged = [this]()
            {
                updateLayout(lastAvailableWidth > 0 ? lastAvailableWidth : getWidth());
            };
            strip->setSections(secAdsr, secLfo, secDynMod);
            addAndMakeVisible(strip);
            strips.add(strip);
        }
        activeCount = count;
    }

    std::function<void(int)> onParamEditedByUser;
    std::function<void(int)> onEnsureKeyframes;

    void setMinimizeAll(bool min)
    {
        for (auto* strip : strips)
            strip->setMinimized(min);
        updateLayout(lastAvailableWidth > 0 ? lastAvailableWidth : getWidth());
    }

    void setSections(bool adsr, bool lfo, bool dynMod)
    {
        secAdsr = adsr;
        secLfo = lfo;
        secDynMod = dynMod;
        for (auto* strip : strips)
            strip->setSections(adsr, lfo, dynMod);
        updateLayout(lastAvailableWidth > 0 ? lastAvailableWidth : getWidth());
    }

    void setActiveCount(int count)
    {
        activeCount = juce::jlimit(1, strips.size(), count);
        for (int i = 0; i < strips.size(); ++i)
            strips[i]->setVisible(i < activeCount);

        updateLayout(lastAvailableWidth > 0 ? lastAvailableWidth : getWidth());
    }

    void refreshAllStrips()
    {
        for (auto* strip : strips)
            strip->refreshState();
        updateLayout(lastAvailableWidth > 0 ? lastAvailableWidth : getWidth());
    }

    int getActiveCount() const { return activeCount; }

    void setSelectedPartial(int index, juce::Viewport* viewport = nullptr)
    {
        for (int i = 0; i < strips.size(); ++i)
            strips[i]->setSelected(i == index);

        if (viewport != nullptr && index >= 0 && index < activeCount && index < strips.size())
        {
            int targetY = strips[index]->getY();
            int cardH = strips[index]->getHeight();
            int viewY = juce::jlimit(0, juce::jmax(0, getHeight() - viewport->getHeight()),
                                     targetY - viewport->getHeight() / 2 + cardH / 2);
            viewport->setViewPosition(0, viewY);
        }
    }

    int calculateTotalHeight(int availableWidth) const
    {
        if (availableWidth <= 0 || activeCount <= 0) return 10;

        int targetCardW = 230;
        int spacing = 8;
        int numCols = juce::jmax(1, (availableWidth + spacing) / (targetCardW + spacing));

        int totalH = 0;
        int rowStart = 0;
        while (rowStart < activeCount && rowStart < strips.size())
        {
            int rowEnd = juce::jmin(activeCount, rowStart + numCols);
            int rowH = 0;
            for (int i = rowStart; i < rowEnd; ++i)
                rowH = juce::jmax(rowH, strips[i]->getDesiredHeight());

            totalH += rowH;
            if (rowEnd < activeCount)
                totalH += spacing;

            rowStart = rowEnd;
        }
        return totalH + 6;
    }

    void updateLayout(int availableWidth)
    {
        if (availableWidth <= 0) availableWidth = getWidth();
        if (availableWidth <= 0) availableWidth = 230;
        lastAvailableWidth = availableWidth;

        int totalH = calculateTotalHeight(availableWidth);
        setSize(availableWidth, totalH);
        resized();
    }

    void resized() override
    {
        int availableWidth = getWidth();
        if (availableWidth <= 0 || activeCount <= 0) return;

        int targetCardW = 230;
        int spacing = 8;
        int numCols = juce::jmax(1, (availableWidth + spacing) / (targetCardW + spacing));
        int actualCardW = (availableWidth - (numCols - 1) * spacing) / numCols;

        int curY = 0;
        int rowStart = 0;
        while (rowStart < activeCount && rowStart < strips.size())
        {
            int rowEnd = juce::jmin(activeCount, rowStart + numCols);
            int rowH = 0;
            for (int i = rowStart; i < rowEnd; ++i)
                rowH = juce::jmax(rowH, strips[i]->getDesiredHeight());

            for (int i = rowStart; i < rowEnd; ++i)
            {
                int c = i - rowStart;
                int x = c * (actualCardW + spacing);
                strips[i]->setBounds(x, curY, actualCardW, rowH);
            }

            curY += rowH + spacing;
            rowStart = rowEnd;
        }
    }

private:
    juce::OwnedArray<PartialStrip> strips;
    int activeCount = 16;
    int lastAvailableWidth = 230;
    bool secAdsr = true;
    bool secLfo = false;
    bool secDynMod = false;
};

//==============================================================================
// Sidebar tool for toggling visualizer graphs and managing grid view
class SideBarTool : public juce::Component
{
public:
    SideBarTool()
    {
        // --- VIEWS ---
        viewsLabel.setText("VIEWS", juce::dontSendNotification);
        viewsLabel.setFont(juce::Font(10.0f, juce::Font::bold));
        viewsLabel.setJustificationType(juce::Justification::centred);
        viewsLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.6f));
        addAndMakeVisible(viewsLabel);

        graphsButton.setButtonText("GRAPHS");
        graphsButton.setClickingTogglesState(true);
        graphsButton.setToggleState(true, juce::dontSendNotification);
        graphsButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF252525));
        graphsButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFF444444));
        graphsButton.setColour(juce::TextButton::textColourOnId, juce::Colour(0xFFFFFFFF));
        graphsButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xFF777777));
        graphsButton.setTooltip("Toggle Visualizer Graphs (Open/Close)");
        addAndMakeVisible(graphsButton);
        graphsButton.onClick = [this]()
        {
            bool open = graphsButton.getToggleState();
            specButton.setToggleState(open, juce::dontSendNotification);
            scopeButton.setToggleState(open, juce::dontSendNotification);
            if (onToggleGraphs) onToggleGraphs(open);
        };

        specButton.setButtonText("SPEC");
        specButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF252525));
        specButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFF444444));
        specButton.setColour(juce::TextButton::textColourOnId, juce::Colour(0xFFFFFFFF));
        specButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xFF777777));
        specButton.setTooltip("Toggle Spectrum Analyzer Display");
        addAndMakeVisible(specButton);
        specButton.onClick = [this]()
        {
            if (onToggleSpec) onToggleSpec();
        };

        // Waterfall toggle (alternate graph that replaces the spectrum display)
        waterfallButton.setButtonText("WFALL");
        waterfallButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF252525));
        waterfallButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFF444444));
        waterfallButton.setColour(juce::TextButton::textColourOnId,  juce::Colour(0xFFFFFFFF));
        waterfallButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xFF777777));
        waterfallButton.setTooltip("Toggle Waterfall Spectrogram (alternates with Spectrum)");
        addAndMakeVisible(waterfallButton);
        waterfallButton.onClick = [this]()
        {
            if (onToggleWaterfall) onToggleWaterfall();
        };

        scopeButton.setButtonText("SCOPE");
        scopeButton.setClickingTogglesState(true);
        scopeButton.setToggleState(true, juce::dontSendNotification);
        scopeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF252525));
        scopeButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFF444444));
        scopeButton.setColour(juce::TextButton::textColourOnId, juce::Colour(0xFFFFFFFF));
        scopeButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xFF777777));
        scopeButton.setTooltip("Toggle Oscilloscope Display");
        addAndMakeVisible(scopeButton);
        scopeButton.onClick = [this]()
        {
            bool scope = scopeButton.getToggleState();
            if (onToggleScope) onToggleScope(scope);
        };

        // --- PARTIALS ---
        partialsLabel.setText("PARTIALS", juce::dontSendNotification);
        partialsLabel.setFont(juce::Font(10.0f, juce::Font::bold));
        partialsLabel.setJustificationType(juce::Justification::centred);
        partialsLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.6f));
        addAndMakeVisible(partialsLabel);

        minAllButton.setButtonText("COLLAPSE");
        minAllButton.setClickingTogglesState(true);
        minAllButton.setToggleState(false, juce::dontSendNotification);
        minAllButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF252525));
        minAllButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFF444444));
        minAllButton.setColour(juce::TextButton::textColourOnId, juce::Colour(0xFFFFFFFF));
        minAllButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xFF888888));
        minAllButton.setTooltip("Collapse / Expand All Partials");
        addAndMakeVisible(minAllButton);
        minAllButton.onClick = [this]()
        {
            bool min = minAllButton.getToggleState();
            minAllButton.setButtonText(min ? "EXPAND" : "COLLAPSE");
            if (onToggleMinAll) onToggleMinAll(min);
        };

        // --- MODULATION ---
        modLabel.setText("MODULATION", juce::dontSendNotification);
        modLabel.setFont(juce::Font(9.0f, juce::Font::bold));
        modLabel.setJustificationType(juce::Justification::centred);
        modLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.6f));
        addAndMakeVisible(modLabel);

        auto setupSecBtn = [this](juce::TextButton& btn, const juce::String& text, const juce::String& tip, bool defaultState)
        {
            btn.setButtonText(text);
            btn.setClickingTogglesState(true);
            btn.setToggleState(defaultState, juce::dontSendNotification);
            btn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF202020));
            btn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFF444444));
            btn.setColour(juce::TextButton::textColourOnId, juce::Colour(0xFFFFFFFF));
            btn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xFF666666));
            btn.setTooltip(tip);
            addAndMakeVisible(btn);
        };

        setupSecBtn(adsrSectionBtn,  "ADSR",    "Show / hide ADSR Envelope in partials", true);
        adsrSectionBtn.onClick = [this]()
        {
            if (onToggleSections)
                onToggleSections(adsrSectionBtn.getToggleState(),
                                 lfoSectionBtn.getToggleState());
        };

        setupSecBtn(lfoSectionBtn,   "LFO",     "Show / hide LFO section in partials", false);
        lfoSectionBtn.onClick = [this]()
        {
            if (onToggleSections)
                onToggleSections(adsrSectionBtn.getToggleState(),
                                 lfoSectionBtn.getToggleState());
        };

        setupSecBtn(animateButton,   "ANIMATE", "Show / hide Keyframe Animation Timeline Panel & Controls", false);
        animateButton.onClick = [this]()
        {
            bool open = animateButton.getToggleState();
            if (onToggleAnimate) onToggleAnimate(open);
        };

        // --- MASTER ---
        masterLabel.setText("MASTER", juce::dontSendNotification);
        masterLabel.setFont(juce::Font(9.0f, juce::Font::bold));
        masterLabel.setJustificationType(juce::Justification::centred);
        masterLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.6f));
        addAndMakeVisible(masterLabel);

        auto setupPanelBtn = [this](juce::TextButton& btn, const juce::String& text, const juce::String& tip, bool defaultState)
        {
            btn.setButtonText(text);
            btn.setClickingTogglesState(false);
            btn.setToggleState(defaultState, juce::dontSendNotification);
            btn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF202020));
            btn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFF444444));
            btn.setColour(juce::TextButton::textColourOnId, juce::Colour(0xFFFFFFFF));
            btn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xFF666666));
            btn.setTooltip(tip);
            addAndMakeVisible(btn);
        };

        setupPanelBtn(voicePanelBtn,  "VOICE",   "View Master Voice & Portamento Settings", false);
        setupPanelBtn(envPanelBtn,    "ENV",     "View Master ADSR Envelope Settings", true);
        setupPanelBtn(filterPanelBtn, "FILTERS", "View Master Low Pass & High Pass Filters", false);

        voicePanelBtn.onClick = [this]()
        {
            setMasterPanelState(MasterPanel::Voice);
            if (onSelectMasterPanel) onSelectMasterPanel(MasterPanel::Voice);
        };

        envPanelBtn.onClick = [this]()
        {
            setMasterPanelState(MasterPanel::Envelope);
            if (onSelectMasterPanel) onSelectMasterPanel(MasterPanel::Envelope);
        };

        filterPanelBtn.onClick = [this]()
        {
            setMasterPanelState(MasterPanel::Filters);
            if (onSelectMasterPanel) onSelectMasterPanel(MasterPanel::Filters);
        };

        // --- PERFORM ---
        performLabel.setText("PERFORM", juce::dontSendNotification);
        performLabel.setFont(juce::Font(9.0f, juce::Font::bold));
        performLabel.setJustificationType(juce::Justification::centred);
        performLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.6f));
        addAndMakeVisible(performLabel);

        velToggle.setButtonText("VEL");
        velToggle.setTooltip("Enable / Disable Velocity Sensitivity (unchecked = fixed full volume)");
        velToggle.setColour(juce::ToggleButton::textColourId, juce::Colour(0xFFFFFFFF));
        velToggle.setColour(juce::ToggleButton::tickColourId, juce::Colour(0xFFFFFFFF));
        velToggle.setColour(juce::ToggleButton::tickDisabledColourId, juce::Colour(0xFF666666));
        addAndMakeVisible(velToggle);

        panicButton.setButtonText("PANIC");
        panicButton.setTooltip("MIDI Panic: Stop all sounding notes immediately (Esc)");
        panicButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF333333));
        panicButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xFFFFFFFF));
        addAndMakeVisible(panicButton);
        panicButton.onClick = [this]()
        {
            if (onPanic) onPanic();
        };
    }

    void setupVelocityAttachment(juce::AudioProcessorValueTreeState& apvts)
    {
        velAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(apvts, "VELOCITY_ENABLED", velToggle);
    }

    void updateGraphButtons(bool graphsOpen, bool specOpen, bool isWaterfall, bool scopeOpen)
    {
        graphsButton.setToggleState(graphsOpen, juce::dontSendNotification);
        specButton.setToggleState(specOpen && !isWaterfall, juce::dontSendNotification);
        waterfallButton.setToggleState(specOpen && isWaterfall, juce::dontSendNotification);
        scopeButton.setToggleState(scopeOpen, juce::dontSendNotification);
    }

    enum class MasterPanel { Envelope, Voice, Filters };

    void setMasterPanelState(MasterPanel panel)
    {
        voicePanelBtn.setToggleState(panel == MasterPanel::Voice, juce::dontSendNotification);
        envPanelBtn.setToggleState(panel == MasterPanel::Envelope, juce::dontSendNotification);
        filterPanelBtn.setToggleState(panel == MasterPanel::Filters, juce::dontSendNotification);
    }

    void setMinAllState(bool min)
    {
        minAllButton.setToggleState(min, juce::dontSendNotification);
        minAllButton.setButtonText(min ? "EXPAND" : "COLLAPSE");
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(4);

        // --- VIEWS ---
        viewsLabel.setBounds(area.removeFromTop(15));
        area.removeFromTop(2);
        graphsButton.setBounds(area.removeFromTop(24));
        area.removeFromTop(3);
        specButton.setBounds(area.removeFromTop(20));
        area.removeFromTop(3);
        waterfallButton.setBounds(area.removeFromTop(20));
        area.removeFromTop(3);
        scopeButton.setBounds(area.removeFromTop(20));

        // --- PARTIALS ---
        area.removeFromTop(7);
        partialsLabel.setBounds(area.removeFromTop(15));
        area.removeFromTop(2);
        minAllButton.setBounds(area.removeFromTop(24));

        // --- MODULATION ---
        area.removeFromTop(7);
        modLabel.setBounds(area.removeFromTop(15));
        area.removeFromTop(2);
        adsrSectionBtn.setBounds(area.removeFromTop(20));
        area.removeFromTop(3);
        lfoSectionBtn.setBounds(area.removeFromTop(20));
        area.removeFromTop(3);
        animateButton.setBounds(area.removeFromTop(20));

        // --- MASTER ---
        area.removeFromTop(7);
        masterLabel.setBounds(area.removeFromTop(15));
        area.removeFromTop(2);
        voicePanelBtn.setBounds(area.removeFromTop(20));
        area.removeFromTop(3);
        envPanelBtn.setBounds(area.removeFromTop(20));
        area.removeFromTop(3);
        filterPanelBtn.setBounds(area.removeFromTop(20));

        // --- PERFORM ---
        area.removeFromTop(7);
        performLabel.setBounds(area.removeFromTop(15));
        area.removeFromTop(2);
        velToggle.setBounds(area.removeFromTop(20));
        area.removeFromTop(3);
        panicButton.setBounds(area.removeFromTop(20));
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced(1.0f);
        g.setColour(juce::Colour(0xFF202020));
        g.fillRoundedRectangle(bounds, 6.0f);
        g.setColour(juce::Colour(0xFF353535));
        g.drawRoundedRectangle(bounds, 6.0f, 1.0f);

        // Separator lines
        float sepY1 = (float)partialsLabel.getY() - 4.0f;
        g.setColour(juce::Colour(0xFF353535));
        g.drawLine(6.0f, sepY1, bounds.getWidth() - 6.0f, sepY1, 1.0f);

        float sepY2 = (float)modLabel.getY() - 4.0f;
        g.drawLine(6.0f, sepY2, bounds.getWidth() - 6.0f, sepY2, 1.0f);

        float sepY3 = (float)masterLabel.getY() - 4.0f;
        g.drawLine(6.0f, sepY3, bounds.getWidth() - 6.0f, sepY3, 1.0f);

        float sepY4 = (float)performLabel.getY() - 4.0f;
        g.drawLine(6.0f, sepY4, bounds.getWidth() - 6.0f, sepY4, 1.0f);
    }

    bool isAdsrVisible() const  { return adsrSectionBtn.getToggleState(); }
    bool isLfoVisible() const   { return lfoSectionBtn.getToggleState(); }
    bool isAnimateOpen() const   { return animateButton.getToggleState(); }
    void setAnimateOpen(bool open) { animateButton.setToggleState(open, juce::dontSendNotification); }
    bool isTimelineOpen() const  { return isAnimateOpen(); }
    void setTimelineOpen(bool open) { setAnimateOpen(open); }

    std::function<void(bool)> onToggleGraphs;
    std::function<void()> onToggleSpec;
    std::function<void()> onToggleWaterfall;
    std::function<void(bool)> onToggleScope;
    std::function<void(bool)> onToggleAnimate;
    std::function<void(bool)> onToggleTimeline;
    std::function<void(bool)> onToggleMinAll;
    std::function<void(bool adsr, bool lfo)> onToggleSections;
    std::function<void(MasterPanel)> onSelectMasterPanel;
    std::function<void()> onPanic;

private:
    juce::Label viewsLabel;
    juce::TextButton graphsButton;
    juce::TextButton specButton;
    juce::TextButton scopeButton;
    juce::TextButton waterfallButton;

    juce::Label partialsLabel;
    juce::TextButton minAllButton;

    juce::Label modLabel;
    juce::TextButton adsrSectionBtn;
    juce::TextButton lfoSectionBtn;
    juce::TextButton animateButton;

    juce::Label masterLabel;
    juce::TextButton voicePanelBtn;
    juce::TextButton envPanelBtn;
    juce::TextButton filterPanelBtn;

    juce::Label performLabel;
    juce::ToggleButton velToggle;
    juce::TextButton panicButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> velAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SideBarTool)
};

//==============================================================================
class WaveSlaveAudioProcessorEditor : public juce::AudioProcessorEditor,
                                      public juce::ValueTree::Listener,
                                      public juce::AudioProcessorValueTreeState::Listener
{
public:
    enum class VisualizerMode { Dual, HarmonicsOnly, ScopeOnly, SpectroscopeOnly, PolarScope };

    WaveSlaveAudioProcessorEditor (WaveSlaveAudioProcessor&);
    ~WaveSlaveAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void setNumPartials(int newCount);
    void updatePartialsLayout(int count);
    void applyBulkWaveform(int waveTypeIndex);
    void applyBulkHarmonicSeries(int seriesType);
    void applyBulkGainTilt(int tiltType);
    void applyBulkDetune(int spreadType);
    void applyBulkSemitones(int semitoneValue);
    void applyBulkPan(int panType);
    void applyBulkVoiceMode(int mode);
    void applyBulkModTarget(int targetIndex);
    void applyBulkModWaveform(int waveIndex);
    void applyBulkModDepth(float depthValue);
    void resetAllPartials();
    void copyAllPartials();
    void pasteAllPartials();
    void randomizePartials();
    void applyFactoryPreset(int index);

    void parameterChanged(const juce::String& parameterID, float newValue) override;

    void valueTreePropertyChanged(juce::ValueTree& treeWhosePropertyHasChanged, const juce::Identifier& property) override;
    void valueTreeChildAdded(juce::ValueTree&, juce::ValueTree&) override {}
    void valueTreeChildRemoved(juce::ValueTree&, juce::ValueTree&, int) override {}
    void valueTreeChildOrderChanged(juce::ValueTree&, int, int) override {}
    void valueTreeParentChanged(juce::ValueTree&) override {}

    // Keyboard / MIDI Panic (Esc)
    bool keyPressed(const juce::KeyPress& key) override;

    void updateVoiceDesc();

private:
    WaveSlaveAudioProcessor& audioProcessor;
    WaveSlaveLookAndFeel customLookAndFeel;

    juce::ComboBox fileMenuBox;
    juce::ComboBox editMenuBox;    // Combined Edit & Bulk transformations in header
    std::unique_ptr<juce::FileChooser> fileChooser;
    bool isApplyingPreset = false;

    struct PartialSnapshot
    {
        bool enabled = true;
        bool adsrEnabled = true;
        float ratio = 1.0f;
        float semitone = 0.0f;
        float detune = 0.0f;
        float gain = 0.0f;
        float pan = 0.0f;
        float waveform = 0.0f;
        float attack = 0.01f, decay = 0.3f, sustain = 1.0f, release = 0.5f;
        float glide = 0.0f;
        float modTarget = 0.0f, modWaveform = 0.0f, modRate = 2.0f, modDepth = 0.0f, modPhase = 0.0f;
        float animMode = 0.0f, animCurve = 0.0f, animDuration = 2.0f, animDepth = 1.0f;
        PartialAnimationData animTrack;
    };
    std::vector<PartialSnapshot> partialsClipboard;

    Oscilloscope oscilloscope;
    Spectrograph spectrograph;
    KeyframeTimeline timelineDock { spectrograph };

    // Master Gain & Pan horizontal sliders
    LabeledSlider masterGainSlider { "Master", false };
    LabeledSlider masterPanSlider { "Pan", false };

    // Factory Presets
    juce::ComboBox factoryPresetBox;
    juce::ComboBox partialsComboBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> partialsAttachment;

    // ADSR
    LabeledSlider attackVSlider  { "Attack", false };
    LabeledSlider decayVSlider   { "Decay", false };
    LabeledSlider sustainVSlider { "Sustain", false };
    LabeledSlider releaseVSlider { "Release", false };

    // Master Filters UI (Low Pass & High Pass)
    juce::Label lpfSectionLabel;
    juce::TextButton lpfEnableButton;
    LabeledSlider lpfCutoffSlider { "Cutoff", false };
    LabeledSlider lpfResoSlider   { "Reso", false };

    juce::Label hpfSectionLabel;
    juce::TextButton hpfEnableButton;
    LabeledSlider hpfCutoffSlider { "Cutoff", false };
    LabeledSlider hpfResoSlider   { "Reso", false };

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> lpfEnableAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lpfCutoffAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lpfResoAttachment;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> hpfEnableAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> hpfCutoffAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> hpfResoAttachment;

    // Master Voice Settings UI
    juce::Label voiceSectionLabel;
    juce::ComboBox masterVoiceBox;
    LabeledSlider masterGlideSlider { "Glide", false };
    juce::Label voiceModeDescLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> masterVoiceAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> masterGlideAttachment;

    SideBarTool::MasterPanel currentMasterPanel = SideBarTool::MasterPanel::Envelope;

    // Snapshot of preset parameter values to detect actual setting modifications
    std::unordered_map<juce::String, float> presetSnapshotValues;
    void recordPresetSnapshot();
    bool hasAnyParameterDivergedFromSnapshot() const;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> masterGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> masterPanAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> numPartialsAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attackAttachment, decayAttachment, sustainAttachment, releaseAttachment;

    // Partials
    juce::Viewport partialsViewport;
    PartialsContainer partialsContainer;

    // Section labels
    juce::Label titleLabel, partialsLabel;
    juce::Label outputLabel, mainEnvLabel;

    // Sidebar view tool
    SideBarTool sideBar;
    bool isGraphsOpen = true;
    bool isSpecOpen = true;
    bool isScopeOpen = true;
    bool isWaterfall = false;
    bool isTimelineOpen = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveSlaveAudioProcessorEditor)
};
