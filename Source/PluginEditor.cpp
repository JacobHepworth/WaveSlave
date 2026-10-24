#include "PluginProcessor.h"
#include "PluginEditor.h"

WaveSlaveAudioProcessorEditor::WaveSlaveAudioProcessorEditor (WaveSlaveAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p), oscilloscope(p), spectrograph(p)
{
    setLookAndFeel(&customLookAndFeel);

    // Visualizers
    addAndMakeVisible(oscilloscope);
    addAndMakeVisible(spectrograph);

    // Title
    titleLabel.setText("WAVESLAVE", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(20.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xFFFFFFFF));
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(titleLabel);

    // Presets
    addAndMakeVisible(factoryPresetBox);
    factoryPresetBox.setTextWhenNothingSelected("Presets");
    factoryPresetBox.addItem("Reset", 1);
    factoryPresetBox.addItem("Fat Saw Bass", 2);
    factoryPresetBox.addItem("Lush Chorus Pad", 3);
    factoryPresetBox.addItem("Drawbar Organ", 4);
    factoryPresetBox.addItem("Warm Polysynth", 5);
    factoryPresetBox.addItem("Acid Bass", 6);
    factoryPresetBox.addItem("FM Electric Piano", 7);
    factoryPresetBox.addItem("Analog Strings", 8);
    factoryPresetBox.addItem("Fibonacci Pluck", 9);
    factoryPresetBox.addItem("Sharkfin Brass", 10);
    factoryPresetBox.addItem("Chiptune", 11);
    factoryPresetBox.addItem("Wobbling Square", 12);
    factoryPresetBox.addItem("Too Much Glue!", 13);
    factoryPresetBox.addItem("Alien Drone", 14);
    factoryPresetBox.addItem("Soft Pad", 15);
    factoryPresetBox.addItem("Seraphim", 16);
    factoryPresetBox.addItem("808 Drum Bass", 17);
    factoryPresetBox.addItem("Random", 18);
    factoryPresetBox.setSelectedId(0, juce::dontSendNotification);
    factoryPresetBox.onChange = [this] 
    { 
        int id = factoryPresetBox.getSelectedId();
        if (id >= 1) applyFactoryPreset(id);
    };
    
    addAndMakeVisible(fileMenuBox);
    fileMenuBox.setTextWhenNothingSelected("File");
    fileMenuBox.addItem("Save Preset...", 1);
    fileMenuBox.addItem("Load Preset...", 2);
    fileMenuBox.setSelectedId(0, juce::dontSendNotification);
    fileMenuBox.onChange = [this]() {
        int id = fileMenuBox.getSelectedId();
        if (id == 1) // Save
        {
            fileChooser = std::make_unique<juce::FileChooser>("Save Preset", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.xml");
            auto folderChooserFlags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles;
            fileChooser->launchAsync(folderChooserFlags, [this](const juce::FileChooser& chooser) {
                juce::File result = chooser.getResult();
                if (result.existsAsFile()) result.deleteFile();
                if (result != juce::File{}) {
                    juce::XmlElement presetXml("WaveSlavePreset");
                    presetXml.setAttribute("version", "1.0");

                    auto state = audioProcessor.apvts.copyState();
                    if (auto paramXml = state.createXml())
                        presetXml.addChildElement(paramXml.release());

                    auto* wtXml = presetXml.createNewChildElement("CustomWavetables");
                    for (int s = 0; s < 4; ++s)
                    {
                        auto* slotXml = wtXml->createNewChildElement("Slot");
                        slotXml->setAttribute("index", s);

                        juce::MemoryOutputStream mos;
                        for (int i = 0; i < 512; ++i)
                            mos.writeFloat(audioProcessor.customWavetables[s][i].load(std::memory_order_relaxed));
                        slotXml->setAttribute("samples", mos.getMemoryBlock().toBase64Encoding());

                        const auto& points = oscilloscope.getSlotSplinePoints(s);
                        juce::String ptStr;
                        for (int p = 0; p < (int)points.size(); ++p)
                        {
                            if (p > 0) ptStr << ";";
                            ptStr << juce::String(points[p].x, 4) << "," << juce::String(points[p].y, 4);
                        }
                        slotXml->setAttribute("splinePoints", ptStr);
                    }

                    audioProcessor.serializeAnimationsToXml(presetXml);

                    presetXml.writeTo(result);
                }
            });
        }
        else if (id == 2) // Load
        {
            fileChooser = std::make_unique<juce::FileChooser>("Load Preset", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.xml");
            auto folderChooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
            fileChooser->launchAsync(folderChooserFlags, [this](const juce::FileChooser& chooser) {
                juce::File result = chooser.getResult();
                if (result.existsAsFile()) {
                    std::unique_ptr<juce::XmlElement> rootXml(juce::XmlDocument::parse(result));
                    if (rootXml != nullptr)
                    {
                        isApplyingPreset = true;

                        juce::XmlElement* paramXml = nullptr;
                        juce::XmlElement* wtXml = nullptr;

                        if (rootXml->hasTagName("WaveSlavePreset"))
                        {
                            paramXml = rootXml->getChildByName(audioProcessor.apvts.state.getType().toString());
                            wtXml = rootXml->getChildByName("CustomWavetables");
                        }
                        else if (rootXml->hasTagName(audioProcessor.apvts.state.getType()))
                        {
                            paramXml = rootXml.get();
                            wtXml = rootXml->getChildByName("CustomWavetables");
                        }

                        if (paramXml != nullptr)
                        {
                            audioProcessor.apvts.replaceState(juce::ValueTree::fromXml(*paramXml));
                        }

                        audioProcessor.deserializeAnimationsFromXml(*rootXml);
                        if (paramXml != nullptr && paramXml != rootXml.get() && rootXml->getChildByName("PartialAnimations") == nullptr)
                        {
                            audioProcessor.deserializeAnimationsFromXml(*paramXml);
                        }

                        if (wtXml != nullptr)
                        {
                            for (auto* slotXml : wtXml->getChildIterator())
                            {
                                int s = slotXml->getIntAttribute("index", -1);
                                if (s >= 0 && s < 4)
                                {
                                    juce::MemoryBlock mb;
                                    mb.fromBase64Encoding(slotXml->getStringAttribute("samples"));
                                    if (mb.getSize() >= 512 * sizeof(float))
                                    {
                                        juce::MemoryInputStream mis(mb, false);
                                        for (int i = 0; i < 512; ++i)
                                            audioProcessor.customWavetables[s][i].store(mis.readFloat(), std::memory_order_relaxed);
                                    }

                                    juce::String ptStr = slotXml->getStringAttribute("splinePoints");
                                    if (ptStr.isNotEmpty())
                                    {
                                        auto& splineVec = oscilloscope.getSlotSplinePoints(s);
                                        splineVec.clear();
                                        auto tokens = juce::StringArray::fromTokens(ptStr, ";", "");
                                        for (auto& tok : tokens)
                                        {
                                            auto xy = juce::StringArray::fromTokens(tok, ",", "");
                                            if (xy.size() == 2)
                                                splineVec.push_back({ xy[0].getFloatValue(), xy[1].getFloatValue() });
                                        }
                                    }
                                }
                            }
                        }

                        isApplyingPreset = false;

                        int activeNum = (int)audioProcessor.apvts.getRawParameterValue("NUM_PARTIALS")->load();
                        partialsContainer.setActiveCount(activeNum);
                        partialsContainer.refreshAllStrips();
                        updatePartialsLayout(activeNum);
                        updateVoiceDesc();
                        resized();
                        recordPresetSnapshot();
                        factoryPresetBox.setSelectedId(0, juce::dontSendNotification);
                        factoryPresetBox.setTextWhenNothingSelected("Custom");
                        spectrograph.syncTimelineControlsToSelectedPartial();
                        oscilloscope.repaint();
                        spectrograph.repaint();
                    }
                }
            });
        }
        // Reset selection back to "File"
        fileMenuBox.setSelectedId(0, juce::dontSendNotification);
    };

    setWantsKeyboardFocus(true);

    // Master section category labels
    outputLabel.setText("OUTPUT", juce::dontSendNotification);
    outputLabel.setFont(juce::Font(10.0f, juce::Font::bold));
    outputLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.6f));
    outputLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(outputLabel);


    mainEnvLabel.setText("ENVELOPE", juce::dontSendNotification);
    mainEnvLabel.setFont(juce::Font(10.0f, juce::Font::bold));
    mainEnvLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.6f));
    mainEnvLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(mainEnvLabel);

    // Partials section label
    partialsLabel.setText("PARTIALS", juce::dontSendNotification);
    partialsLabel.setFont(juce::Font(11.0f, juce::Font::bold));
    partialsLabel.setColour(juce::Label::textColourId, juce::Colour(0xFFFFFFFF));
    partialsLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(partialsLabel);

    // Global Master Gain & Pan sliders
    addAndMakeVisible(masterGainSlider);
    masterGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "MASTER_GAIN", masterGainSlider.slider);

    addAndMakeVisible(masterPanSlider);
    masterPanSlider.slider.setDoubleClickReturnValue(true, 0.0);
    masterPanAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "MASTER_PAN", masterPanSlider.slider);



    addAndMakeVisible(partialsComboBox);
    partialsComboBox.addItem("None", 1);
    for (int i = 1; i <= 15; ++i)
    {
        partialsComboBox.addItem(juce::String(i) + (i == 1 ? " Partial" : " Partials"), i + 1);
    }

    // Combined Edit & Bulk Transformations dropdown
    addAndMakeVisible(editMenuBox);
    editMenuBox.setTextWhenNothingSelected("Edit");

    editMenuBox.addItem("Reset All Partials",       99);
    editMenuBox.addItem("Randomize All Partials",   98);
    editMenuBox.addItem("Copy All Partials",         97);
    editMenuBox.addItem("Paste All Partials",        96);
    editMenuBox.addSeparator();

    editMenuBox.addItem("Waveform: All Sine",          1);
    editMenuBox.addItem("Waveform: All Triangle",      2);
    editMenuBox.addItem("Waveform: All Sawtooth",      3);
    editMenuBox.addItem("Waveform: All Square",        4);
    editMenuBox.addItem("Waveform: All Pulse",         5);
    editMenuBox.addItem("Waveform: All Sharkfin",      6);
    editMenuBox.addItem("Waveform: All Fibonacci",     7);
    editMenuBox.addItem("Waveform: All Custom 1",      8);
    editMenuBox.addItem("Waveform: All Custom 2",      9);
    editMenuBox.addItem("Waveform: All Custom 3",      100);
    editMenuBox.addItem("Waveform: All Custom 4",      101);
    editMenuBox.addSeparator();

    editMenuBox.addItem("Ratios: Natural Series",    10);
    editMenuBox.addItem("Ratios: Odd Harmonics",     11);
    editMenuBox.addItem("Ratios: Even Harmonics",    12);
    editMenuBox.addItem("Ratios: Octaves",           13);
    editMenuBox.addItem("Ratios: Inharmonic / Bell", 14);
    editMenuBox.addSeparator();

    editMenuBox.addItem("Gain: 1/n Rolloff",            20);
    editMenuBox.addItem("Gain: 1/n^2 Rolloff",          21);
    editMenuBox.addItem("Gain: Flat (Equal)",           22);
    editMenuBox.addItem("Gain: Inverted",               23);
    editMenuBox.addItem("Gain: Fundamental Only",       24);
    editMenuBox.addSeparator();

    editMenuBox.addItem("Pan: Center All",           35);
    editMenuBox.addItem("Pan: Alternate L/R",        36);
    editMenuBox.addItem("Pan: Wide L/R",             37);
    editMenuBox.addSeparator();

    editMenuBox.addItem("Voice: All Poly",           60);
    editMenuBox.addItem("Voice: All Mono",           61);
    editMenuBox.addItem("Voice: All Legato",         62);
    editMenuBox.addSeparator();

    editMenuBox.addItem("Detune: None",              30);
    editMenuBox.addItem("Detune: Subtle Spread",     31);
    editMenuBox.addItem("Detune: Wide Spread",       32);
    editMenuBox.addSeparator();

    editMenuBox.addItem("Semitone: Reset All",       40);
    editMenuBox.addSeparator();

    editMenuBox.addItem("LFO Target: None",          50);
    editMenuBox.addItem("LFO Target: Pitch",         51);
    editMenuBox.addItem("LFO Target: Level",         52);
    editMenuBox.addItem("LFO Depth: 50%",            53);
    editMenuBox.addSeparator();

    editMenuBox.addItem("LFO Wave: All Sine",        110);
    editMenuBox.addItem("LFO Wave: All Triangle",    111);
    editMenuBox.addItem("LFO Wave: All Saw",         112);
    editMenuBox.addItem("LFO Wave: All Square",      113);
    editMenuBox.addItem("LFO Wave: All Custom 1",    114);
    editMenuBox.addItem("LFO Wave: All Custom 2",    115);
    editMenuBox.addItem("LFO Wave: All Custom 3",    116);
    editMenuBox.addItem("LFO Wave: All Custom 4",    117);

    editMenuBox.onChange = [this]()
    {
        int result = editMenuBox.getSelectedId();
        if (result == 0) return;

        if      (result == 99) resetAllPartials();
        else if (result == 98) randomizePartials();
        else if (result == 97) copyAllPartials();
        else if (result == 96) pasteAllPartials();
        else if (result == 1) applyBulkWaveform(0);
        else if (result == 2) applyBulkWaveform(1);
        else if (result == 3) applyBulkWaveform(2);
        else if (result == 4) applyBulkWaveform(3);
        else if (result == 5) applyBulkWaveform(4); // Pulse
        else if (result == 6) applyBulkWaveform(5); // Sharkfin
        else if (result == 7) applyBulkWaveform(6); // Fibonacci
        else if (result == 8) applyBulkWaveform(8); // Custom 1
        else if (result == 9) applyBulkWaveform(9); // Custom 2
        else if (result == 100) applyBulkWaveform(10); // Custom 3
        else if (result == 101) applyBulkWaveform(11); // Custom 4
        else if (result == 10) applyBulkHarmonicSeries(0);
        else if (result == 11) applyBulkHarmonicSeries(1);
        else if (result == 12) applyBulkHarmonicSeries(2);
        else if (result == 13) applyBulkHarmonicSeries(3);
        else if (result == 14) applyBulkHarmonicSeries(4);
        else if (result == 20) applyBulkGainTilt(0);
        else if (result == 21) applyBulkGainTilt(1);
        else if (result == 22) applyBulkGainTilt(2);
        else if (result == 23) applyBulkGainTilt(3);
        else if (result == 24) applyBulkGainTilt(4);
        else if (result == 30) applyBulkDetune(0);
        else if (result == 31) applyBulkDetune(1);
        else if (result == 32) applyBulkDetune(2);
        else if (result == 35) applyBulkPan(0);
        else if (result == 36) applyBulkPan(1);
        else if (result == 37) applyBulkPan(2);
        else if (result == 60) applyBulkVoiceMode(0);
        else if (result == 61) applyBulkVoiceMode(1);
        else if (result == 62) applyBulkVoiceMode(2);
        else if (result == 40) applyBulkSemitones(0);
        else if (result == 50) applyBulkModTarget(0);
        else if (result == 51) applyBulkModTarget(1);
        else if (result == 52) applyBulkModTarget(2);
        else if (result == 53) applyBulkModDepth(0.5f);
        else if (result == 110) applyBulkModWaveform(0);
        else if (result == 111) applyBulkModWaveform(1);
        else if (result == 112) applyBulkModWaveform(2);
        else if (result == 113) applyBulkModWaveform(3);
        else if (result == 114) applyBulkModWaveform(4); // Custom 1
        else if (result == 115) applyBulkModWaveform(5); // Custom 2
        else if (result == 116) applyBulkModWaveform(6); // Custom 3
        else if (result == 117) applyBulkModWaveform(7); // Custom 4

        editMenuBox.setSelectedId(0, juce::dontSendNotification);
    };

    // ADSR
    addAndMakeVisible(attackVSlider);
    attackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "ATTACK", attackVSlider.slider);

    addAndMakeVisible(decayVSlider);
    decayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "DECAY", decayVSlider.slider);

    addAndMakeVisible(sustainVSlider);
    sustainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "SUSTAIN", sustainVSlider.slider);

    addAndMakeVisible(releaseVSlider);
    releaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "RELEASE", releaseVSlider.slider);

    // Low Pass Filter UI
    addChildComponent(lpfSectionLabel);
    lpfSectionLabel.setText("LOW PASS FILTER", juce::dontSendNotification);
    lpfSectionLabel.setFont(juce::Font(10.0f, juce::Font::bold));
    lpfSectionLabel.setJustificationType(juce::Justification::centredLeft);
    lpfSectionLabel.setColour(juce::Label::textColourId, juce::Colour(0xFFFFFFFF));

    addChildComponent(lpfEnableButton);
    lpfEnableButton.setButtonText("ON");
    lpfEnableButton.setClickingTogglesState(true);
    lpfEnableButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF252525));
    lpfEnableButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFF444444));
    lpfEnableButton.setColour(juce::TextButton::textColourOnId, juce::Colour(0xFFFFFFFF));
    lpfEnableButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xFF666666));
    lpfEnableAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.apvts, "LPF_ENABLED", lpfEnableButton);

    addChildComponent(lpfCutoffSlider);
    lpfCutoffSlider.slider.setTextValueSuffix(" Hz");
    lpfCutoffAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "LPF_CUTOFF", lpfCutoffSlider.slider);

    addChildComponent(lpfResoSlider);
    lpfResoAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "LPF_RESO", lpfResoSlider.slider);

    // High Pass Filter UI
    addChildComponent(hpfSectionLabel);
    hpfSectionLabel.setText("HIGH PASS FILTER", juce::dontSendNotification);
    hpfSectionLabel.setFont(juce::Font(10.0f, juce::Font::bold));
    hpfSectionLabel.setJustificationType(juce::Justification::centredLeft);
    hpfSectionLabel.setColour(juce::Label::textColourId, juce::Colour(0xFFFFFFFF));

    addChildComponent(hpfEnableButton);
    hpfEnableButton.setButtonText("ON");
    hpfEnableButton.setClickingTogglesState(true);
    hpfEnableButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF252525));
    hpfEnableButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFF444444));
    hpfEnableButton.setColour(juce::TextButton::textColourOnId, juce::Colour(0xFFFFFFFF));
    hpfEnableButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xFF666666));
    hpfEnableAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.apvts, "HPF_ENABLED", hpfEnableButton);

    addChildComponent(hpfCutoffSlider);
    hpfCutoffSlider.slider.setTextValueSuffix(" Hz");
    hpfCutoffAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "HPF_CUTOFF", hpfCutoffSlider.slider);

    addChildComponent(hpfResoSlider);
    hpfResoAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "HPF_RESO", hpfResoSlider.slider);

    auto updateFilterButtonsText = [this]() {
        bool lOn = lpfEnableButton.getToggleState();
        lpfEnableButton.setButtonText(lOn ? "ON" : "OFF");
        lpfCutoffSlider.setEnabled(lOn);
        lpfResoSlider.setEnabled(lOn);
        lpfCutoffSlider.setAlpha(lOn ? 1.0f : 0.45f);
        lpfResoSlider.setAlpha(lOn ? 1.0f : 0.45f);

        bool hOn = hpfEnableButton.getToggleState();
        hpfEnableButton.setButtonText(hOn ? "ON" : "OFF");
        hpfCutoffSlider.setEnabled(hOn);
        hpfResoSlider.setEnabled(hOn);
        hpfCutoffSlider.setAlpha(hOn ? 1.0f : 0.45f);
        hpfResoSlider.setAlpha(hOn ? 1.0f : 0.45f);
    };
    lpfEnableButton.onStateChange = updateFilterButtonsText;
    hpfEnableButton.onStateChange = updateFilterButtonsText;
    updateFilterButtonsText();

    // Master Voice Settings UI
    addChildComponent(voiceSectionLabel);
    voiceSectionLabel.setText("VOICE MODE", juce::dontSendNotification);
    voiceSectionLabel.setFont(juce::Font(10.0f, juce::Font::bold));
    voiceSectionLabel.setJustificationType(juce::Justification::centredLeft);
    voiceSectionLabel.setColour(juce::Label::textColourId, juce::Colour(0xFFFFFFFF));

    addChildComponent(masterVoiceBox);
    masterVoiceBox.addItemList({ "Poly", "Mono", "Legato" }, 1);
    masterVoiceAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.apvts, "VOICE_MODE", masterVoiceBox);

    addChildComponent(masterGlideSlider);
    masterGlideSlider.slider.setTextValueSuffix(" s");
    masterGlideAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "GLIDE_TIME", masterGlideSlider.slider);

    addChildComponent(voiceModeDescLabel);
    voiceModeDescLabel.setFont(juce::Font(10.0f));
    voiceModeDescLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.6f));
    voiceModeDescLabel.setJustificationType(juce::Justification::centredLeft);

    masterVoiceBox.onChange = [this]() { updateVoiceDesc(); };
    updateVoiceDesc();

    // Partials viewport (vertical scroll on right side)
    addAndMakeVisible(partialsViewport);
    partialsViewport.setViewedComponent(&partialsContainer, false);
    partialsViewport.setScrollBarsShown(true, false); // vertical scroll only

    // Two-way connection between spectrum pucks and partial strip cards
    spectrograph.onPartialSelected = [this](int idx)
    {
        partialsContainer.setSelectedPartial(idx, &partialsViewport);
    };

    spectrograph.onAnimationChanged = [this]()
    {
        partialsContainer.refreshAllStrips();
    };

    partialsContainer.onParamEditedByUser = [this](int idx)
    {
        spectrograph.recordKeyframeFromSliders(idx);
    };

    partialsContainer.onEnsureKeyframes = [this](int idx)
    {
        if (auto* p = audioProcessor.apvts.getRawParameterValue("ANIM_MODE" + juce::String(idx + 1)))
        {
            if ((int)p->load() > 0)
                audioProcessor.ensureInitialKeyframes(idx);
        }
        spectrograph.syncTimelineControlsToSelectedPartial();
        spectrograph.repaint();
    };

    partialsContainer.initPartials(audioProcessor.apvts, 16, [this](int idx)
    {
        spectrograph.setSelectedPartial(idx);
    });

    // Dynamic partials count change hook
    partialsComboBox.onChange = [this]()
    {
        int count = partialsComboBox.getSelectedId();
        if (count < 1) count = 1;
        setNumPartials(count);
    };

    int initialCount = juce::jlimit(1, 16, (int)audioProcessor.apvts.getRawParameterValue("NUM_PARTIALS")->load());
    partialsContainer.setActiveCount(initialCount);
    partialsComboBox.setSelectedId(initialCount, juce::dontSendNotification);
    partialsLabel.setVisible(true);
    partialsViewport.setVisible(true);
    if (initialCount == 1)
        partialsLabel.setText("BASE NOTE ONLY", juce::dontSendNotification);
    else
        partialsLabel.setText("PARTIALS  (Base + " + juce::String(initialCount - 1) + (initialCount == 2 ? " Partial)" : " Partials)"), juce::dontSendNotification);

    audioProcessor.apvts.state.addListener(this);
    audioProcessor.apvts.addParameterListener("NUM_PARTIALS", this);

    // Sidebar tool setup
    addAndMakeVisible(sideBar);
    sideBar.setupVelocityAttachment(audioProcessor.apvts);
    sideBar.onToggleGraphs = [this](bool open)
    {
        isGraphsOpen = open;
        isSpecOpen = open;
        isScopeOpen = open;
        spectrograph.setWaterfallMode(isWaterfall);
        sideBar.updateGraphButtons(isGraphsOpen, isSpecOpen, isWaterfall, isScopeOpen);
        resized();
        repaint();
    };

    sideBar.onToggleSpec = [this]()
    {
        if (isSpecOpen && !isWaterfall)
        {
            // Already showing Spectrum -> toggle upper graph off
            isSpecOpen = false;
        }
        else
        {
            // Was off or in Waterfall mode -> switch to Spectrum mode
            isSpecOpen = true;
            isWaterfall = false;
            spectrograph.setWaterfallMode(false);
        }
        isGraphsOpen = isSpecOpen || isScopeOpen;
        sideBar.updateGraphButtons(isGraphsOpen, isSpecOpen, isWaterfall, isScopeOpen);
        resized();
        repaint();
    };

    sideBar.onToggleWaterfall = [this]()
    {
        if (isSpecOpen && isWaterfall)
        {
            // Already in Waterfall mode -> alternate back to Spectrum!
            isWaterfall = false;
            spectrograph.setWaterfallMode(false);
        }
        else
        {
            // Was off or in Spectrum mode -> alternate to Waterfall!
            isSpecOpen = true;
            isWaterfall = true;
            spectrograph.setWaterfallMode(true);
        }
        isGraphsOpen = isSpecOpen || isScopeOpen;
        sideBar.updateGraphButtons(isGraphsOpen, isSpecOpen, isWaterfall, isScopeOpen);
        resized();
        repaint();
    };

    sideBar.onToggleScope = [this](bool open)
    {
        isScopeOpen = open;
        isGraphsOpen = isSpecOpen || isScopeOpen;
        sideBar.updateGraphButtons(isGraphsOpen, isSpecOpen, isWaterfall, isScopeOpen);
        resized();
        repaint();
    };

    addChildComponent(timelineDock);
    spectrograph.setTimelineOpen(false);
    partialsContainer.setSections(sideBar.isAdsrVisible(), sideBar.isLfoVisible(), false);
    auto toggleAnimCallback = [this](bool open)
    {
        isTimelineOpen = open;
        spectrograph.setTimelineOpen(open);
        timelineDock.setVisible(open);
        partialsContainer.setSections(sideBar.isAdsrVisible(), sideBar.isLfoVisible(), open);
        if (open)
        {
            spectrograph.syncSlidersToPlayhead(spectrograph.getSelectedPartial());
        }
        resized();
        repaint();
    };
    sideBar.onToggleAnimate  = toggleAnimCallback;
    sideBar.onToggleTimeline = toggleAnimCallback;

    sideBar.updateGraphButtons(isGraphsOpen, isSpecOpen, isWaterfall, isScopeOpen);

    sideBar.onToggleMinAll = [this](bool min)
    {
        partialsContainer.setMinimizeAll(min);
    };

    sideBar.onToggleSections = [this](bool adsr, bool lfo)
    {
        partialsContainer.setSections(adsr, lfo, isTimelineOpen);
    };

    sideBar.onPanic = [this]()
    {
        audioProcessor.allNotesOff();
    };

    sideBar.onSelectMasterPanel = [this](SideBarTool::MasterPanel panel)
    {
        currentMasterPanel = panel;
        resized();
        repaint();
    };
    sideBar.setMasterPanelState(currentMasterPanel);

    recordPresetSnapshot();

    setResizable(true, true);
    setResizeLimits(880, 560, 2560, 1600);
    setSize (940, 660);
}

WaveSlaveAudioProcessorEditor::~WaveSlaveAudioProcessorEditor()
{
    audioProcessor.apvts.removeParameterListener("NUM_PARTIALS", this);
    audioProcessor.apvts.state.removeListener(this);
    setLookAndFeel(nullptr);
}

void WaveSlaveAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Main background gradient - standard dark theme
    juce::ColourGradient bgGrad(juce::Colour(0xFF181818), 0.0f, 0.0f,
                                 juce::Colour(0xFF181818), 0.0f, (float)getHeight(), false);
    g.setGradientFill(bgGrad);
    g.fillAll();

    int numPartials = partialsContainer.getActiveCount();
    float sidebarW = 52.0f;
    float startX = 10.0f + sidebarW + 8.0f;
    float totalW = (float)getWidth() - startX - 10.0f;
    float timelineH = 76.0f;
    float timelineOffset = isTimelineOpen ? (timelineH + 8.0f) : 0.0f;
    bool showSpectrograph = isGraphsOpen && isSpecOpen;
    bool showScope = isGraphsOpen && isScopeOpen;

    if (isTimelineOpen)
    {
        auto tlPanelRect = juce::Rectangle<float>(startX, (float)getHeight() - 10.0f - timelineH, totalW, timelineH);
        g.setColour(juce::Colour(0xFF202020));
        g.fillRoundedRectangle(tlPanelRect, 6.0f);
        g.setColour(juce::Colour(0xFF353535));
        g.drawRoundedRectangle(tlPanelRect, 6.0f, 1.0f);
    }

    if (!showSpectrograph)
    {
        // =====================================================================
        // GRAPH (SPECTROGRAPH) IS MINIMIZED:
        // The partials grid pops up into the empty top space!
        // =====================================================================
        float bottomH = showScope ? 200.0f : 84.0f;
        float spacing = 8.0f;
        float topH = (float)getHeight() - 48.0f - timelineOffset - bottomH - spacing;

        // Top Partials Grid background panel
        if (numPartials > 0)
        {
            auto gridArea = juce::Rectangle<float>(startX, 38.0f, totalW, topH);
            g.setColour(juce::Colour(0xFF202020));
            g.fillRoundedRectangle(gridArea, 6.0f);
            g.setColour(juce::Colour(0xFF353535));
            g.drawRoundedRectangle(gridArea, 6.0f, 1.0f);
        }

        // Bottom Row (Scope + Master, or expanded Master) background panel
        auto bottomAreaRect = juce::Rectangle<float>(startX, 38.0f + topH + spacing, totalW, bottomH);
        g.setColour(juce::Colour(0xFF202020));
        g.fillRoundedRectangle(bottomAreaRect, 6.0f);
        g.setColour(juce::Colour(0xFF353535));
        g.drawRoundedRectangle(bottomAreaRect, 6.0f, 1.0f);
    }
    else
    {
        // =====================================================================
        // GRAPH (SPECTROGRAPH) IS OPEN:
        // Left: Spectrograph (top) + Bottom row (Scope/Master)
        // Right: Harmonics Pane
        // =====================================================================
        int harmonicsWidth = (numPartials > 0) ? 246 : 0;
        float leftWidth = (numPartials > 0) ? totalW - (float)harmonicsWidth - 8.0f
                                            : totalW;
        auto leftArea = juce::Rectangle<float>(startX, 38.0f, leftWidth, (float)getHeight() - 48.0f - timelineOffset);
        g.setColour(juce::Colour(0xFF202020));
        g.fillRoundedRectangle(leftArea, 6.0f);
        g.setColour(juce::Colour(0xFF353535));
        g.drawRoundedRectangle(leftArea, 6.0f, 1.0f);

        // Right-Side Harmonics Pane
        if (numPartials > 0)
        {
            auto rightArea = juce::Rectangle<float>(startX + leftWidth + 8.0f, 38.0f, (float)harmonicsWidth, (float)getHeight() - 48.0f - timelineOffset);
            g.setColour(juce::Colour(0xFF202020));
            g.fillRoundedRectangle(rightArea, 6.0f);
            g.setColour(juce::Colour(0xFF353535));
            g.drawRoundedRectangle(rightArea, 6.0f, 1.0f);
        }
    }

    // Category separator line between Output and Envelope / Filter / Voice area
    bool hasRightSection = mainEnvLabel.isVisible() || lpfSectionLabel.isVisible() || hpfSectionLabel.isVisible() || voiceSectionLabel.isVisible();
    if (outputLabel.isVisible() && hasRightSection)
    {
        int rightLabelX = mainEnvLabel.isVisible() ? mainEnvLabel.getX()
                        : (voiceSectionLabel.isVisible() ? voiceSectionLabel.getX()
                        : (lpfSectionLabel.isVisible() ? lpfSectionLabel.getX() : hpfSectionLabel.getX()));
        g.setColour(juce::Colour(0xFF333333));
        float sepColX = (float)rightLabelX - 8.0f;
        float topY = (float)outputLabel.getY() - 2.0f;
        float botY = (oscilloscope.isVisible()) ? ((float)outputLabel.getY() + 160.0f)
                                               : juce::jmax((float)masterPanSlider.getBottom(), (float)outputLabel.getY() + 65.0f);
        if (sepColX > 0 && botY > topY)
            g.drawLine(sepColX, topY, sepColX, botY, 1.0f);
    }
}

void WaveSlaveAudioProcessorEditor::parameterChanged(const juce::String& parameterID, float newValue)
{
    if (parameterID == "NUM_PARTIALS")
    {
        int count = juce::jlimit(1, 16, (int)std::round(newValue));
        juce::MessageManager::callAsync([this, count]()
        {
            if (partialsComboBox.getSelectedId() != count)
                partialsComboBox.setSelectedId(count, juce::dontSendNotification);
            updatePartialsLayout(count);
        });
    }
}

void WaveSlaveAudioProcessorEditor::updatePartialsLayout(int count)
{
    count = juce::jlimit(1, 16, count);
    partialsContainer.setActiveCount(count);
    
    partialsLabel.setVisible(true);
    partialsViewport.setVisible(true);
    
    if (count == 1)
        partialsLabel.setText("BASE NOTE ONLY", juce::dontSendNotification);
    else
        partialsLabel.setText("PARTIALS  (Base + " + juce::String(count - 1) + (count == 2 ? " Partial)" : " Partials)"), juce::dontSendNotification);

    resized();
    repaint();
    spectrograph.repaint();
    oscilloscope.repaint();
}

void WaveSlaveAudioProcessorEditor::setNumPartials(int numPartials)
{
    numPartials = juce::jlimit(1, 16, numPartials);
    if (auto* p = audioProcessor.apvts.getParameter("NUM_PARTIALS"))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1((float)numPartials));
        p->endChangeGesture();
    }
    updatePartialsLayout(numPartials);
}

void WaveSlaveAudioProcessorEditor::applyBulkWaveform(int waveTypeIndex)
{
    for (int i = 1; i <= 16; ++i)
    {
        if (auto* p = audioProcessor.apvts.getParameter("WAVEFORM" + juce::String(i)))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost(p->convertTo0to1((float)waveTypeIndex));
            p->endChangeGesture();
        }
    }
}

void WaveSlaveAudioProcessorEditor::applyBulkHarmonicSeries(int seriesType)
{
    for (int i = 1; i <= 16; ++i)
    {
        if (auto* p = audioProcessor.apvts.getParameter("RATIO" + juce::String(i)))
        {
            float ratio = (float)i;
            if (seriesType == 1) ratio = (float)(i * 2 - 1); // Odd
            else if (seriesType == 2) ratio = (float)(i * 2); // Even
            else if (seriesType == 3) ratio = std::pow(2.0f, (float)(i - 1)); // Octaves
            else if (seriesType == 4) ratio = 1.0f + 0.5f * (i - 1); // Inharmonic/Bell

            p->beginChangeGesture();
            p->setValueNotifyingHost(p->convertTo0to1(juce::jlimit(0.01f, 32.0f, ratio)));
            p->endChangeGesture();
        }
    }
}

void WaveSlaveAudioProcessorEditor::applyBulkGainTilt(int tiltType)
{
    for (int i = 1; i <= 16; ++i)
    {
        if (auto* p = audioProcessor.apvts.getParameter("GAIN" + juce::String(i)))
        {
            float g = 1.0f;
            if (tiltType == 0) g = 1.0f / i; // 1/n
            else if (tiltType == 1) g = 1.0f / (i * i); // 1/n^2
            else if (tiltType == 2) g = (i == 1) ? 1.0f : 0.5f; // Flat
            else if (tiltType == 3) g = (float)i / 16.0f; // Inverted
            else if (tiltType == 4) g = (i == 1) ? 1.0f : 0.05f; // Solo Fundamental

            p->beginChangeGesture();
            p->setValueNotifyingHost(p->convertTo0to1(juce::jlimit(0.0f, 1.0f, g)));
            p->endChangeGesture();
        }
    }
}

void WaveSlaveAudioProcessorEditor::applyBulkDetune(int detuneType)
{
    for (int i = 1; i <= 16; ++i)
    {
        if (auto* p = audioProcessor.apvts.getParameter("DETUNE" + juce::String(i)))
        {
            float d = 0.0f;
            if (detuneType == 1) d = (i % 2 == 0 ? 1.0f : -1.0f) * (i * 0.5f); // Subtle spread
            else if (detuneType == 2) d = (i % 2 == 0 ? 1.0f : -1.0f) * (i * 2.0f); // Wide spread

            p->beginChangeGesture();
            p->setValueNotifyingHost(p->convertTo0to1(juce::jlimit(-50.0f, 50.0f, d)));
            p->endChangeGesture();
        }
    }
}

void WaveSlaveAudioProcessorEditor::applyBulkSemitones(int semitones)
{
    for (int i = 1; i <= 16; ++i)
    {
        if (auto* p = audioProcessor.apvts.getParameter("SEMITONE" + juce::String(i)))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost(p->convertTo0to1(juce::jlimit(-24.0f, 24.0f, (float)semitones)));
            p->endChangeGesture();
        }
    }
}

void WaveSlaveAudioProcessorEditor::applyBulkPan(int panType)
{
    for (int i = 1; i <= 16; ++i)
    {
        if (auto* p = audioProcessor.apvts.getParameter("PAN" + juce::String(i)))
        {
            float pan = 0.0f;
            if (panType == 1) // Alternate L/R
                pan = (i % 2 == 1) ? -0.5f : 0.5f;
            else if (panType == 2) // Wide alternate
                pan = (i % 2 == 1) ? -1.0f : 1.0f;

            p->beginChangeGesture();
            p->setValueNotifyingHost(p->convertTo0to1(juce::jlimit(-1.0f, 1.0f, pan)));
            p->endChangeGesture();
        }
    }
}

void WaveSlaveAudioProcessorEditor::applyBulkVoiceMode(int mode)
{
    if (auto* vm = audioProcessor.apvts.getParameter("VOICE_MODE"))
    {
        float targetVal = vm->convertTo0to1((float)mode);
        if (std::abs(vm->getValue() - targetVal) > 0.001f)
        {
            vm->beginChangeGesture();
            vm->setValueNotifyingHost(targetVal);
            vm->endChangeGesture();
        }
    }
}

void WaveSlaveAudioProcessorEditor::applyBulkModTarget(int targetIndex)
{
    for (int i = 1; i <= 16; ++i)
    {
        if (auto* p = audioProcessor.apvts.getParameter("MOD_TARGET" + juce::String(i)))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost(p->convertTo0to1((float)targetIndex));
            p->endChangeGesture();
        }
    }
}

void WaveSlaveAudioProcessorEditor::applyBulkModDepth(float depthValue)
{
    for (int i = 1; i <= 16; ++i)
    {
        if (auto* p = audioProcessor.apvts.getParameter("MOD_DEPTH" + juce::String(i)))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost(p->convertTo0to1(juce::jlimit(0.0f, 1.0f, depthValue)));
            p->endChangeGesture();
        }
    }
}

void WaveSlaveAudioProcessorEditor::applyBulkModWaveform(int waveIndex)
{
    for (int i = 1; i <= 16; ++i)
    {
        if (auto* p = audioProcessor.apvts.getParameter("MOD_WAVEFORM" + juce::String(i)))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost(p->convertTo0to1((float)waveIndex));
            p->endChangeGesture();
        }
    }
}

void WaveSlaveAudioProcessorEditor::resetAllPartials()
{
    audioProcessor.clearAllKeyframes();

    for (int i = 1; i <= 16; ++i)
    {
        juce::String s = juce::String(i);
        if (auto* p = audioProcessor.apvts.getParameter("P_ENABLED" + s)) {
            p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(i <= 4 ? 1.0f : 0.0f)); p->endChangeGesture();
        }
        if (auto* p = audioProcessor.apvts.getParameter("WAVEFORM" + s)) {
            p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.0f)); p->endChangeGesture();
        }
        if (auto* p = audioProcessor.apvts.getParameter("RATIO" + s)) {
            p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1((float)i)); p->endChangeGesture();
        }
        if (auto* p = audioProcessor.apvts.getParameter("SEMITONE" + s)) {
            p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.0f)); p->endChangeGesture();
        }
        if (auto* p = audioProcessor.apvts.getParameter("DETUNE" + s)) {
            p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.0f)); p->endChangeGesture();
        }
        if (auto* p = audioProcessor.apvts.getParameter("GAIN" + s)) {
            float g = 1.0f / (float)i;
            p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(g)); p->endChangeGesture();
        }
        if (auto* p = audioProcessor.apvts.getParameter("PAN" + s)) {
            p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.0f)); p->endChangeGesture();
        }
        if (auto* p = audioProcessor.apvts.getParameter("P_ADSR_ENABLED" + s)) {
            p->beginChangeGesture(); p->setValueNotifyingHost(0.0f); p->endChangeGesture();
        }
        if (auto* p = audioProcessor.apvts.getParameter("P_ATTACK" + s)) {
            p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.01f)); p->endChangeGesture();
        }
        if (auto* p = audioProcessor.apvts.getParameter("P_DECAY" + s)) {
            p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.3f)); p->endChangeGesture();
        }
        if (auto* p = audioProcessor.apvts.getParameter("P_SUSTAIN" + s)) {
            p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(1.0f)); p->endChangeGesture();
        }
        if (auto* p = audioProcessor.apvts.getParameter("P_RELEASE" + s)) {
            p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.5f)); p->endChangeGesture();
        }
        if (auto* p = audioProcessor.apvts.getParameter("P_GLIDE" + s)) {
            p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.0f)); p->endChangeGesture();
        }
        if (auto* p = audioProcessor.apvts.getParameter("MOD_TARGET" + s)) {
            p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.0f)); p->endChangeGesture();
        }
        if (auto* p = audioProcessor.apvts.getParameter("MOD_WAVEFORM" + s)) {
            p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.0f)); p->endChangeGesture();
        }
        if (auto* p = audioProcessor.apvts.getParameter("MOD_RATE" + s)) {
            p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(2.0f)); p->endChangeGesture();
        }
        if (auto* p = audioProcessor.apvts.getParameter("MOD_DEPTH" + s)) {
            p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.0f)); p->endChangeGesture();
        }
        if (auto* p = audioProcessor.apvts.getParameter("MOD_PHASE" + s)) {
            p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.0f)); p->endChangeGesture();
        }
        if (auto* p = audioProcessor.apvts.getParameter("ANIM_MODE" + s)) {
            p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.0f)); p->endChangeGesture();
        }
        if (auto* p = audioProcessor.apvts.getParameter("ANIM_CURVE" + s)) {
            p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.0f)); p->endChangeGesture();
        }
        if (auto* p = audioProcessor.apvts.getParameter("ANIM_DEPTH" + s)) {
            p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(1.0f)); p->endChangeGesture();
        }
        if (auto* p = audioProcessor.apvts.getParameter("ANIM_DURATION" + s)) {
            p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(2.0f)); p->endChangeGesture();
        }
    }
    spectrograph.syncTimelineControlsToSelectedPartial();
}

void WaveSlaveAudioProcessorEditor::copyAllPartials()
{
    partialsClipboard.clear();
    partialsClipboard.resize(16);
    for (int i = 1; i <= 16; ++i)
    {
        juce::String s = juce::String(i);
        auto& p = partialsClipboard[i - 1];
        if (auto* param = audioProcessor.apvts.getParameter("P_ENABLED" + s)) p.enabled = (param->getValue() > 0.5f);
        if (auto* param = audioProcessor.apvts.getParameter("RATIO" + s)) p.ratio = param->getValue();
        if (auto* param = audioProcessor.apvts.getParameter("SEMITONE" + s)) p.semitone = param->getValue();
        if (auto* param = audioProcessor.apvts.getParameter("DETUNE" + s)) p.detune = param->getValue();
        if (auto* param = audioProcessor.apvts.getParameter("GAIN" + s)) p.gain = param->getValue();
        if (auto* param = audioProcessor.apvts.getParameter("PAN" + s)) p.pan = param->getValue();
        if (auto* param = audioProcessor.apvts.getParameter("WAVEFORM" + s)) p.waveform = param->getValue();
        if (auto* param = audioProcessor.apvts.getParameter("P_ADSR_ENABLED" + s)) p.adsrEnabled = (param->getValue() > 0.5f);
        if (auto* param = audioProcessor.apvts.getParameter("P_ATTACK" + s)) p.attack = param->getValue();
        if (auto* param = audioProcessor.apvts.getParameter("P_DECAY" + s)) p.decay = param->getValue();
        if (auto* param = audioProcessor.apvts.getParameter("P_SUSTAIN" + s)) p.sustain = param->getValue();
        if (auto* param = audioProcessor.apvts.getParameter("P_RELEASE" + s)) p.release = param->getValue();
        if (auto* param = audioProcessor.apvts.getParameter("P_GLIDE" + s)) p.glide = param->getValue();
        if (auto* param = audioProcessor.apvts.getParameter("MOD_TARGET" + s)) p.modTarget = param->getValue();
        if (auto* param = audioProcessor.apvts.getParameter("MOD_WAVEFORM" + s)) p.modWaveform = param->getValue();
        if (auto* param = audioProcessor.apvts.getParameter("MOD_RATE" + s)) p.modRate = param->getValue();
        if (auto* param = audioProcessor.apvts.getParameter("MOD_DEPTH" + s)) p.modDepth = param->getValue();
        if (auto* param = audioProcessor.apvts.getParameter("MOD_PHASE" + s)) p.modPhase = param->getValue();
        if (auto* param = audioProcessor.apvts.getParameter("ANIM_MODE" + s)) p.animMode = param->getValue();
        if (auto* param = audioProcessor.apvts.getParameter("ANIM_CURVE" + s)) p.animCurve = param->getValue();
        if (auto* param = audioProcessor.apvts.getParameter("ANIM_DURATION" + s)) p.animDuration = param->getValue();
        if (auto* param = audioProcessor.apvts.getParameter("ANIM_DEPTH" + s)) p.animDepth = param->getValue();
        p.animTrack = audioProcessor.getPartialAnimation(i - 1);
    }
}

void WaveSlaveAudioProcessorEditor::pasteAllPartials()
{
    if (partialsClipboard.size() != 16) return;
    for (int i = 1; i <= 16; ++i)
    {
        juce::String s = juce::String(i);
        const auto& p = partialsClipboard[i - 1];
        auto apply = [this](const juce::String& paramId, float normVal)
        {
            if (auto* param = audioProcessor.apvts.getParameter(paramId))
            {
                param->beginChangeGesture();
                param->setValueNotifyingHost(normVal);
                param->endChangeGesture();
            }
        };
        apply("P_ENABLED" + s, p.enabled ? 1.0f : 0.0f);
        apply("RATIO" + s, p.ratio);
        apply("SEMITONE" + s, p.semitone);
        apply("DETUNE" + s, p.detune);
        apply("GAIN" + s, p.gain);
        apply("PAN" + s, p.pan);
        apply("WAVEFORM" + s, p.waveform);
        apply("P_ADSR_ENABLED" + s, p.adsrEnabled ? 1.0f : 0.0f);
        apply("P_ATTACK" + s, p.attack);
        apply("P_DECAY" + s, p.decay);
        apply("P_SUSTAIN" + s, p.sustain);
        apply("P_RELEASE" + s, p.release);
        apply("P_GLIDE" + s, p.glide);
        apply("MOD_TARGET" + s, p.modTarget);
        apply("MOD_WAVEFORM" + s, p.modWaveform);
        apply("MOD_RATE" + s, p.modRate);
        apply("MOD_DEPTH" + s, p.modDepth);
        apply("MOD_PHASE" + s, p.modPhase);
        apply("ANIM_MODE" + s, p.animMode);
        apply("ANIM_CURVE" + s, p.animCurve);
        apply("ANIM_DURATION" + s, p.animDuration);
        apply("ANIM_DEPTH" + s, p.animDepth);
        audioProcessor.setPartialAnimation(i - 1, p.animTrack);
    }
    spectrograph.syncTimelineControlsToSelectedPartial();
    spectrograph.repaint();
    oscilloscope.repaint();
}

void WaveSlaveAudioProcessorEditor::randomizePartials()
{
    int active = partialsContainer.getActiveCount();
    for (int i = 1; i <= active; ++i)
    {
        juce::String s = juce::String(i);
        int wave = std::rand() % 6;
        if (auto* p = audioProcessor.apvts.getParameter("WAVEFORM" + s))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost(p->convertTo0to1((float)wave));
            p->endChangeGesture();
        }
        float detune = ((float)std::rand() / RAND_MAX * 20.0f) - 10.0f;
        if (auto* p = audioProcessor.apvts.getParameter("DETUNE" + s))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost(p->convertTo0to1(detune));
            p->endChangeGesture();
        }
        if (i > 1)
        {
            float baseG = 1.0f / (float)i;
            float jitter = ((float)std::rand() / RAND_MAX * 0.4f) - 0.2f;
            float gain = juce::jlimit(0.01f, 1.0f, baseG * (1.0f + jitter));
            if (auto* p = audioProcessor.apvts.getParameter("GAIN" + s))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost(p->convertTo0to1(gain));
                p->endChangeGesture();
            }
        }
        float pan = ((float)std::rand() / RAND_MAX * 1.6f) - 0.8f;
        if (auto* p = audioProcessor.apvts.getParameter("PAN" + s))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost(p->convertTo0to1(pan));
            p->endChangeGesture();
        }
    }
    spectrograph.repaint();
    oscilloscope.repaint();
}

void WaveSlaveAudioProcessorEditor::recordPresetSnapshot()
{
    presetSnapshotValues.clear();
    for (auto* param : audioProcessor.getParameters())
    {
        if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param))
            presetSnapshotValues[p->paramID] = p->getValue();
    }
}

bool WaveSlaveAudioProcessorEditor::hasAnyParameterDivergedFromSnapshot() const
{
    if (presetSnapshotValues.empty())
        return false;

    for (auto* param : audioProcessor.getParameters())
    {
        if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param))
        {
            auto it = presetSnapshotValues.find(p->paramID);
            if (it != presetSnapshotValues.end())
            {
                if (std::abs(p->getValue() - it->second) > 0.005f)
                    return true;
            }
        }
    }
    return false;
}

// --- helper: set one APVTS parameter by ID ----------------------------------
static void setParam(juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float value)
{
    if (auto* p = apvts.getParameter(id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(value));
        p->endChangeGesture();
    }
}

void WaveSlaveAudioProcessorEditor::applyFactoryPreset(int index)
{
    isApplyingPreset = true;

    // Every preset starts from a known clean state so nothing bleeds from previous.
    resetAllPartials();
    applyBulkSemitones(0);

    // Reset master params to neutral defaults before each preset overrides them
    auto& ap = audioProcessor.apvts;
    setParam(ap, "MASTER_GAIN",  0.5f);
    setParam(ap, "MASTER_PAN",   0.0f);
    setParam(ap, "VOICE_MODE",   0.0f); // Poly
    setParam(ap, "GLIDE_TIME",   0.0f);
    setParam(ap, "BASE_NOTE",   60.0f); // C4
    setParam(ap, "BASE_WAVEFORM", 0.0f);
    setParam(ap, "LPF_ENABLED",  0.0f);
    setParam(ap, "LPF_CUTOFF",   20000.0f);
    setParam(ap, "LPF_RESO",     0.707f);
    setParam(ap, "HPF_ENABLED",  0.0f);
    setParam(ap, "HPF_CUTOFF",   20.0f);
    setParam(ap, "HPF_RESO",     0.707f);
    setParam(ap, "VELOCITY_ENABLED", 1.0f);

    // --- Also zero out all MOD_PHASE, P_GLIDE, and P_ADSR_ENABLED for all partials ---
    for (int i = 1; i <= 16; ++i)
    {
        setParam(ap, "MOD_PHASE"      + juce::String(i), 0.0f);
        setParam(ap, "P_GLIDE"        + juce::String(i), 0.0f);
        setParam(ap, "P_ADSR_ENABLED" + juce::String(i), 0.0f);
    }

    // ------------------------------------------------------------------------
    if (index == 1) // Reset - return to factory defaults
    {
        oscilloscope.clearWavetable();
        setParam(ap, "ATTACK",       0.01f);
        setParam(ap, "DECAY",        0.3f);
        setParam(ap, "SUSTAIN",      0.8f);
        setParam(ap, "RELEASE",      0.5f);
        setParam(ap, "NUM_PARTIALS", 4.0f);
    }

    // ------------------------------------------------------------------------
    else if (index == 2) // Fat Saw Bass - Minimoog Model D standard
    {
        setParam(ap, "NUM_PARTIALS", 4.0f);
        setParam(ap, "VOICE_MODE",   1.0f); // Mono
        setParam(ap, "GLIDE_TIME",   0.05f);
        setParam(ap, "BASE_NOTE",    48.0f); // C3 bass octave
        setParam(ap, "VELOCITY_ENABLED", 1.0f);

        // 3 stacked saw oscillators + octave overtone (Minimoog 32', 16', 8')
        float ratios[4]  = { 1.0f,  1.0f,  1.0f,  2.0f };
        float gains[4]   = { 1.0f,  0.75f, 0.75f, 0.40f };
        float detunes[4] = { 0.0f,  4.0f, -4.0f,  1.5f };
        float pans[4]    = { 0.0f, -0.2f,  0.2f,  0.0f };

        for (int i = 1; i <= 4; ++i) {
            setParam(ap, "WAVEFORM"    + juce::String(i), 2.0f); // Saw
            setParam(ap, "RATIO"       + juce::String(i), ratios[i-1]);
            setParam(ap, "GAIN"        + juce::String(i), gains[i-1]);
            setParam(ap, "DETUNE"      + juce::String(i), detunes[i-1]);
            setParam(ap, "PAN"         + juce::String(i), pans[i-1]);
            setParam(ap, "P_GLIDE"     + juce::String(i), 0.05f);
            setParam(ap, "P_ATTACK"    + juce::String(i), 0.004f);
            setParam(ap, "P_DECAY"     + juce::String(i), 0.32f);
            setParam(ap, "P_SUSTAIN"   + juce::String(i), 0.45f);
            setParam(ap, "P_RELEASE"   + juce::String(i), 0.20f);
            setParam(ap, "MOD_TARGET"  + juce::String(i), 0.0f); // None
            setParam(ap, "MOD_DEPTH"   + juce::String(i), 0.0f);
            setParam(ap, "MOD_RATE"    + juce::String(i), 2.0f);
            setParam(ap, "MOD_WAVEFORM"+ juce::String(i), 0.0f);
            setParam(ap, "ANIM_MODE"   + juce::String(i), 0.0f);
            setParam(ap, "ANIM_CURVE"  + juce::String(i), 0.0f);
            setParam(ap, "ANIM_DEPTH"  + juce::String(i), 1.0f);
            setParam(ap, "ANIM_DURATION"+ juce::String(i), 2.0f);
        }
        for (int i = 5; i <= 16; ++i)
            setParam(ap, "GAIN" + juce::String(i), 0.0f);

        setParam(ap, "ATTACK",       0.004f);
        setParam(ap, "DECAY",        0.32f);
        setParam(ap, "SUSTAIN",      0.45f);
        setParam(ap, "RELEASE",      0.20f);
        setParam(ap, "MASTER_GAIN",  0.56f);
        setParam(ap, "LPF_ENABLED",  1.0f);
        setParam(ap, "LPF_CUTOFF",   850.0f); // 24dB Moog ladder cutoff
        setParam(ap, "LPF_RESO",     2.4f);   // Characteristic ladder bite
    }

    // ------------------------------------------------------------------------
    else if (index == 3) // Lush Chorus Pad - Roland Juno / D-50 ambient pad standard
    {
        setParam(ap, "NUM_PARTIALS", 8.0f);
        setParam(ap, "VOICE_MODE",   0.0f); // Poly
        setParam(ap, "VELOCITY_ENABLED", 1.0f);

        float ratios[8]  = { 1.0f,  2.0f,  3.0f,  4.0f,  5.0f,  6.0f,  7.0f,  8.0f };
        float gains[8]   = { 1.0f,  0.65f, 0.45f, 0.32f, 0.22f, 0.16f, 0.11f, 0.08f };
        float detunes[8] = { 0.0f,  7.0f, -7.0f,  5.0f, -5.0f,  3.0f, -3.0f,  1.0f };
        float pans[8]    = { 0.0f, -0.6f,  0.6f, -0.4f,  0.4f, -0.7f,  0.7f,  0.0f };
        float waves[8]   = { 2.0f,  1.0f,  2.0f,  1.0f,  2.0f,  1.0f,  2.0f,  1.0f }; // Alternating Saw & Tri

        for (int i = 1; i <= 8; ++i) {
            setParam(ap, "WAVEFORM"    + juce::String(i), waves[i-1]);
            setParam(ap, "RATIO"       + juce::String(i), ratios[i-1]);
            setParam(ap, "GAIN"        + juce::String(i), gains[i-1]);
            setParam(ap, "DETUNE"      + juce::String(i), detunes[i-1]);
            setParam(ap, "PAN"         + juce::String(i), pans[i-1]);
            setParam(ap, "P_ATTACK"    + juce::String(i), 1.2f);
            setParam(ap, "P_DECAY"     + juce::String(i), 2.5f);
            setParam(ap, "P_SUSTAIN"   + juce::String(i), 0.85f);
            setParam(ap, "P_RELEASE"   + juce::String(i), 2.5f);
            setParam(ap, "MOD_TARGET"  + juce::String(i), 1.0f); // Pitch (lush chorus vibrato)
            setParam(ap, "MOD_DEPTH"   + juce::String(i), 0.012f);
            setParam(ap, "MOD_RATE"    + juce::String(i), 0.8f + i * 0.05f);
            setParam(ap, "MOD_WAVEFORM"+ juce::String(i), 0.0f); // Sine
            setParam(ap, "MOD_PHASE"   + juce::String(i), (float)((i * 45) % 360));
            setParam(ap, "ANIM_MODE"   + juce::String(i), 0.0f);
            setParam(ap, "ANIM_CURVE"  + juce::String(i), 0.0f);
            setParam(ap, "ANIM_DEPTH"  + juce::String(i), 1.0f);
            setParam(ap, "ANIM_DURATION"+ juce::String(i), 2.0f);
        }
        for (int i = 9; i <= 16; ++i)
            setParam(ap, "GAIN" + juce::String(i), 0.0f);

        setParam(ap, "ATTACK",      1.2f);
        setParam(ap, "DECAY",       2.5f);
        setParam(ap, "SUSTAIN",     0.85f);
        setParam(ap, "RELEASE",     2.5f);
        setParam(ap, "MASTER_GAIN", 0.50f);
        setParam(ap, "HPF_ENABLED", 1.0f);
        setParam(ap, "HPF_CUTOFF",  55.0f);
        setParam(ap, "HPF_RESO",    0.707f);
        setParam(ap, "LPF_ENABLED", 1.0f);
        setParam(ap, "LPF_CUTOFF",  6800.0f);
        setParam(ap, "LPF_RESO",    0.707f);
    }

    // ------------------------------------------------------------------------
    else if (index == 4) // Drawbar Organ - Hammond B3 tonewheel standard
    {
        // Authentic 9 tonewheel footages:
        // 16' (0.5x), 5 1/3' (1.498x), 8' (1.0x), 4' (2.0x), 2 2/3' (2.997x), 2' (4.0x), 1 3/5' (5.0x), 1 1/3' (6.0x), 1' (8.0x)
        setParam(ap, "NUM_PARTIALS", 9.0f);
        setParam(ap, "VOICE_MODE",   0.0f); // Poly
        setParam(ap, "VELOCITY_ENABLED", 0.0f); // Authentic B3 keys have no velocity response

        float ratios[9] = { 0.5f, 1.4983f, 1.0f, 2.0f, 2.9966f, 4.0f, 5.0f, 6.0f, 8.0f };
        float gains[9]  = { 0.85f, 0.70f,  1.0f, 0.85f, 0.65f,   0.75f, 0.40f, 0.50f, 0.30f }; // Full Gospel/Rock registration

        for (int i = 1; i <= 9; ++i) {
            setParam(ap, "WAVEFORM"    + juce::String(i), 0.0f); // Pure Sine tonewheels
            setParam(ap, "RATIO"       + juce::String(i), ratios[i-1]);
            setParam(ap, "GAIN"        + juce::String(i), gains[i-1]);
            setParam(ap, "DETUNE"      + juce::String(i), 0.0f);
            setParam(ap, "PAN"         + juce::String(i), 0.0f);
            setParam(ap, "P_ATTACK"    + juce::String(i), 0.003f); // Instant key click
            setParam(ap, "P_DECAY"     + juce::String(i), 0.05f);
            setParam(ap, "P_SUSTAIN"   + juce::String(i), 1.0f);
            setParam(ap, "P_RELEASE"   + juce::String(i), 0.02f);
            setParam(ap, "MOD_TARGET"  + juce::String(i), 2.0f); // Level (Leslie tremolo)
            setParam(ap, "MOD_DEPTH"   + juce::String(i), 0.16f);
            setParam(ap, "MOD_RATE"    + juce::String(i), 6.4f); // Fast Leslie rotor speed
            setParam(ap, "MOD_WAVEFORM"+ juce::String(i), 0.0f); // Sine
            setParam(ap, "MOD_PHASE"   + juce::String(i), (float)((i * 40) % 360));
            setParam(ap, "ANIM_MODE"   + juce::String(i), 0.0f);
            setParam(ap, "ANIM_CURVE"  + juce::String(i), 0.0f);
            setParam(ap, "ANIM_DEPTH"  + juce::String(i), 1.0f);
            setParam(ap, "ANIM_DURATION"+ juce::String(i), 2.0f);
        }
        for (int i = 10; i <= 16; ++i)
            setParam(ap, "GAIN" + juce::String(i), 0.0f);

        setParam(ap, "ATTACK",      0.003f);
        setParam(ap, "DECAY",       0.05f);
        setParam(ap, "SUSTAIN",     1.0f);
        setParam(ap, "RELEASE",     0.02f);
        setParam(ap, "MASTER_GAIN", 0.55f);
    }

    // ------------------------------------------------------------------------
    else if (index == 5) // Warm Polysynth - Prophet-5 / Jupiter-8 standard
    {
        setParam(ap, "NUM_PARTIALS", 6.0f);
        setParam(ap, "VOICE_MODE",   0.0f); // Poly
        setParam(ap, "VELOCITY_ENABLED", 1.0f);

        float ratios[6]  = { 1.0f,  1.0f,  2.0f,  2.0f,  3.0f,  4.0f };
        float gains[6]   = { 1.0f,  0.80f, 0.45f, 0.35f, 0.20f, 0.12f };
        float waves[6]   = { 2.0f,  4.0f,  2.0f,  4.0f,  1.0f,  0.0f }; // Saw, Pulse, Saw, Pulse, Tri, Sine
        float detunes[6] = { 0.0f,  5.0f, -4.0f,  3.0f, -2.0f,  1.0f };
        float pans[6]    = { 0.0f, -0.3f,  0.3f, -0.4f,  0.4f,  0.0f };

        for (int i = 1; i <= 6; ++i) {
            setParam(ap, "WAVEFORM"    + juce::String(i), waves[i-1]);
            setParam(ap, "RATIO"       + juce::String(i), ratios[i-1]);
            setParam(ap, "GAIN"        + juce::String(i), gains[i-1]);
            setParam(ap, "DETUNE"      + juce::String(i), detunes[i-1]);
            setParam(ap, "PAN"         + juce::String(i), pans[i-1]);
            setParam(ap, "P_ATTACK"    + juce::String(i), 0.035f);
            setParam(ap, "P_DECAY"     + juce::String(i), 0.60f);
            setParam(ap, "P_SUSTAIN"   + juce::String(i), 0.65f);
            setParam(ap, "P_RELEASE"   + juce::String(i), 0.50f);
            setParam(ap, "MOD_TARGET"  + juce::String(i), 0.0f); // None
            setParam(ap, "MOD_DEPTH"   + juce::String(i), 0.0f);
            setParam(ap, "MOD_RATE"    + juce::String(i), 2.0f);
            setParam(ap, "MOD_WAVEFORM"+ juce::String(i), 0.0f);
            setParam(ap, "ANIM_MODE"   + juce::String(i), 0.0f);
            setParam(ap, "ANIM_CURVE"  + juce::String(i), 0.0f);
            setParam(ap, "ANIM_DEPTH"  + juce::String(i), 1.0f);
            setParam(ap, "ANIM_DURATION"+ juce::String(i), 2.0f);
        }
        for (int i = 7; i <= 16; ++i)
            setParam(ap, "GAIN" + juce::String(i), 0.0f);

        setParam(ap, "ATTACK",      0.035f);
        setParam(ap, "DECAY",       0.60f);
        setParam(ap, "SUSTAIN",     0.65f);
        setParam(ap, "RELEASE",     0.50f);
        setParam(ap, "MASTER_GAIN", 0.52f);
        setParam(ap, "LPF_ENABLED", 1.0f);
        setParam(ap, "LPF_CUTOFF",  3200.0f); // Warm 4-pole poly cutoff
        setParam(ap, "LPF_RESO",    1.4f);
    }

    // ------------------------------------------------------------------------
    else if (index == 6) // Acid Bass - Roland TB-303 standard
    {
        setParam(ap, "NUM_PARTIALS", 4.0f);
        setParam(ap, "VOICE_MODE",   2.0f); // Legato (slide)
        setParam(ap, "GLIDE_TIME",   0.08f);
        setParam(ap, "BASE_NOTE",    48.0f); // C3 bass octave
        setParam(ap, "VELOCITY_ENABLED", 1.0f);

        float ratios[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
        float gains[4]  = { 1.0f, 0.50f, 0.25f, 0.10f };

        for (int i = 1; i <= 4; ++i) {
            setParam(ap, "WAVEFORM"    + juce::String(i), 2.0f); // Saw
            setParam(ap, "RATIO"       + juce::String(i), ratios[i-1]);
            setParam(ap, "GAIN"        + juce::String(i), gains[i-1]);
            setParam(ap, "DETUNE"      + juce::String(i), 0.0f);
            setParam(ap, "PAN"         + juce::String(i), 0.0f);
            setParam(ap, "P_GLIDE"     + juce::String(i), 0.08f);
            setParam(ap, "P_ATTACK"    + juce::String(i), 0.002f);
            setParam(ap, "P_DECAY"     + juce::String(i), 0.18f);
            setParam(ap, "P_SUSTAIN"   + juce::String(i), 0.0f);
            setParam(ap, "P_RELEASE"   + juce::String(i), 0.10f);
            setParam(ap, "MOD_TARGET"  + juce::String(i), 1.0f); // Pitch vibrato
            setParam(ap, "MOD_DEPTH"   + juce::String(i), 0.015f);
            setParam(ap, "MOD_RATE"    + juce::String(i), 7.5f);
            setParam(ap, "MOD_WAVEFORM"+ juce::String(i), 1.0f); // Triangle
            setParam(ap, "ANIM_MODE"   + juce::String(i), 1.0f); // One-Shot punch animation
            setParam(ap, "ANIM_CURVE"  + juce::String(i), 0.0f); // Smooth
            setParam(ap, "ANIM_DEPTH"  + juce::String(i), 1.0f);
            setParam(ap, "ANIM_DURATION"+ juce::String(i), 0.22f);

            PartialAnimationData track;
            track.keyframes[0] = { 0.0f, ratios[i-1] * 1.15f, juce::jlimit(0.0f, 1.0f, gains[i-1] * 1.15f), 0.0f, 0.0f, 0.0f };
            track.keyframes[1] = { 1.0f, ratios[i-1],         gains[i-1],                                  0.0f, 0.0f, 0.0f };
            track.numKeyframes = 2;
            audioProcessor.setPartialAnimation(i - 1, track);
        }
        for (int i = 5; i <= 16; ++i)
            setParam(ap, "GAIN" + juce::String(i), 0.0f);

        setParam(ap, "ATTACK",      0.002f);
        setParam(ap, "DECAY",       0.18f);
        setParam(ap, "SUSTAIN",     0.0f);
        setParam(ap, "RELEASE",     0.10f);
        setParam(ap, "MASTER_GAIN", 0.58f);
        setParam(ap, "LPF_ENABLED", 1.0f);
        setParam(ap, "LPF_CUTOFF",  1100.0f); // Famous squelchy TB-303 cutoff
        setParam(ap, "LPF_RESO",    5.2f);    // High resonance peak
    }

    // ------------------------------------------------------------------------
    else if (index == 7) // FM Electric Piano - Yamaha DX7 "E. PIANO 1" standard
    {
        setParam(ap, "NUM_PARTIALS", 8.0f);
        setParam(ap, "VOICE_MODE",   0.0f); // Poly
        setParam(ap, "VELOCITY_ENABLED", 1.0f);

        // DX7 carrier-modulator stack: fundamental + body + bell tine overtones
        float ratios[8]  = { 1.0f,  1.0f,  2.0f,  4.0f,  7.0f,  9.85f, 14.0f, 18.2f };
        float gains[8]   = { 1.0f,  0.65f, 0.40f, 0.25f, 0.32f, 0.22f, 0.15f, 0.08f };
        float detunes[8] = { 0.0f,  1.5f, -1.0f,  2.0f,  0.0f,  1.0f,  0.0f,  0.0f };
        float pans[8]    = { 0.0f, -0.2f,  0.2f, -0.3f,  0.3f, -0.5f,  0.5f,  0.0f };

        // Per-partial decay modeling: fundamental sustains, high tine partials die fast!
        float pDecays[8] = { 1.8f,  1.6f,  1.2f,  0.8f,  0.22f, 0.18f, 0.14f, 0.10f };
        float pSusts[8]  = { 0.25f, 0.20f, 0.10f, 0.05f, 0.0f,  0.0f,  0.0f,  0.0f };

        for (int i = 1; i <= 8; ++i) {
            setParam(ap, "WAVEFORM"    + juce::String(i), 0.0f); // Pure Sine
            setParam(ap, "RATIO"       + juce::String(i), ratios[i-1]);
            setParam(ap, "GAIN"        + juce::String(i), gains[i-1]);
            setParam(ap, "DETUNE"      + juce::String(i), detunes[i-1]);
            setParam(ap, "PAN"         + juce::String(i), pans[i-1]);
            setParam(ap, "P_ADSR_ENABLED" + juce::String(i), (i >= 5 ? 1.0f : 0.0f)); // Tine harmonics decay independently
            setParam(ap, "P_ATTACK"    + juce::String(i), 0.001f);
            setParam(ap, "P_DECAY"     + juce::String(i), pDecays[i-1]);
            setParam(ap, "P_SUSTAIN"   + juce::String(i), pSusts[i-1]);
            setParam(ap, "P_RELEASE"   + juce::String(i), 0.8f);
            setParam(ap, "MOD_TARGET"  + juce::String(i), 0.0f);
            setParam(ap, "MOD_DEPTH"   + juce::String(i), 0.0f);
            setParam(ap, "MOD_RATE"    + juce::String(i), 2.0f);
            setParam(ap, "MOD_WAVEFORM"+ juce::String(i), 0.0f);

            // Keyframe animation: subtle bell tine shimmer on partials 5-8
            setParam(ap, "ANIM_MODE"   + juce::String(i), 0.0f);
            setParam(ap, "ANIM_CURVE"  + juce::String(i), 0.0f);
            setParam(ap, "ANIM_DEPTH"  + juce::String(i), 1.0f);
            setParam(ap, "ANIM_DURATION"+ juce::String(i), 2.0f);
        }
        for (int i = 9; i <= 16; ++i)
            setParam(ap, "GAIN" + juce::String(i), 0.0f);

        setParam(ap, "ATTACK",      0.001f);
        setParam(ap, "DECAY",       1.8f);
        setParam(ap, "SUSTAIN",     0.25f);
        setParam(ap, "RELEASE",     0.8f);
        setParam(ap, "MASTER_GAIN", 0.52f);
        setParam(ap, "HPF_ENABLED", 1.0f);
        setParam(ap, "HPF_CUTOFF",  45.0f);
        setParam(ap, "HPF_RESO",    0.707f);
    }

    // ------------------------------------------------------------------------
    else if (index == 8) // Analog Strings - ARP Solina / Roland Juno-60 standard
    {
        setParam(ap, "NUM_PARTIALS", 8.0f);
        setParam(ap, "VOICE_MODE",   0.0f); // Poly
        setParam(ap, "VELOCITY_ENABLED", 1.0f);

        // 8 saw partials layered in octaves (Solina 8' and 4' string ranks)
        float ratios[8]  = { 1.0f,  1.0f,  2.0f,  2.0f,  3.0f,  3.0f,  4.0f,  4.0f };
        float gains[8]   = { 1.0f,  0.85f, 0.60f, 0.50f, 0.35f, 0.30f, 0.18f, 0.14f };
        float detunes[8] = { 0.0f,  8.0f, -8.0f,  6.0f, -6.0f,  4.0f, -4.0f,  2.0f };
        float pans[8]    = {-0.2f,  0.2f, -0.6f,  0.6f, -0.4f,  0.4f, -0.7f,  0.7f };

        for (int i = 1; i <= 8; ++i) {
            setParam(ap, "WAVEFORM"    + juce::String(i), 2.0f); // Saw
            setParam(ap, "RATIO"       + juce::String(i), ratios[i-1]);
            setParam(ap, "GAIN"        + juce::String(i), gains[i-1]);
            setParam(ap, "DETUNE"      + juce::String(i), detunes[i-1]);
            setParam(ap, "PAN"         + juce::String(i), pans[i-1]);
            setParam(ap, "P_ATTACK"    + juce::String(i), 0.75f);
            setParam(ap, "P_DECAY"     + juce::String(i), 2.5f);
            setParam(ap, "P_SUSTAIN"   + juce::String(i), 0.85f);
            setParam(ap, "P_RELEASE"   + juce::String(i), 1.8f);
            setParam(ap, "MOD_TARGET"  + juce::String(i), 1.0f); // Pitch (Solina 3-phase chorus)
            setParam(ap, "MOD_DEPTH"   + juce::String(i), 0.015f);
            setParam(ap, "MOD_RATE"    + juce::String(i), 0.55f);
            setParam(ap, "MOD_WAVEFORM"+ juce::String(i), 0.0f); // Sine
            setParam(ap, "MOD_PHASE"   + juce::String(i), (float)((i * 45) % 360));
            setParam(ap, "ANIM_MODE"   + juce::String(i), 0.0f);
            setParam(ap, "ANIM_CURVE"  + juce::String(i), 0.0f);
            setParam(ap, "ANIM_DEPTH"  + juce::String(i), 1.0f);
            setParam(ap, "ANIM_DURATION"+ juce::String(i), 2.0f);
        }
        for (int i = 9; i <= 16; ++i)
            setParam(ap, "GAIN" + juce::String(i), 0.0f);

        setParam(ap, "ATTACK",      0.75f);
        setParam(ap, "DECAY",       2.5f);
        setParam(ap, "SUSTAIN",     0.85f);
        setParam(ap, "RELEASE",     1.8f);
        setParam(ap, "MASTER_GAIN", 0.48f);
        setParam(ap, "HPF_ENABLED", 1.0f);
        setParam(ap, "HPF_CUTOFF",  70.0f);
        setParam(ap, "HPF_RESO",    0.707f);
        setParam(ap, "LPF_ENABLED", 1.0f);
        setParam(ap, "LPF_CUTOFF",  7500.0f);
        setParam(ap, "LPF_RESO",    0.707f);
    }

    // ------------------------------------------------------------------------
    else if (index == 9) // Fibonacci Pluck - additive struck bell/string
    {
        setParam(ap, "NUM_PARTIALS", 8.0f);
        setParam(ap, "VOICE_MODE",   0.0f); // Poly
        setParam(ap, "VELOCITY_ENABLED", 1.0f);

        // Fibonacci harmonic ratios: 1, 1.5, 2, 3, 5, 8, 13, 21
        float fib[8]     = { 1.0f,  1.5f,  2.0f,  3.0f,  5.0f,  8.0f,  13.0f, 21.0f };
        float gains[8]   = { 1.0f,  0.70f, 0.48f, 0.30f, 0.18f, 0.10f, 0.05f, 0.02f };
        float detunes[8] = { 0.0f,  2.0f, -2.0f,  1.5f, -1.5f,  1.0f, -1.0f,  0.5f };
        float pans[8]    = { 0.0f, -0.2f,  0.2f, -0.4f,  0.4f, -0.6f,  0.6f,  0.0f };

        // Exponential HF damping: higher partials lose energy exponentially faster
        float pDecays[8] = { 1.2f,  0.8f,  0.5f,  0.35f, 0.22f, 0.14f, 0.08f, 0.04f };

        for (int i = 1; i <= 8; ++i) {
            setParam(ap, "WAVEFORM"    + juce::String(i), 6.0f); // Fibonacci
            setParam(ap, "RATIO"       + juce::String(i), fib[i-1]);
            setParam(ap, "GAIN"        + juce::String(i), gains[i-1]);
            setParam(ap, "DETUNE"      + juce::String(i), detunes[i-1]);
            setParam(ap, "PAN"         + juce::String(i), pans[i-1]);
            setParam(ap, "P_ADSR_ENABLED" + juce::String(i), (i >= 2 ? 1.0f : 0.0f)); // Damping on upper harmonics
            setParam(ap, "P_ATTACK"    + juce::String(i), 0.001f);
            setParam(ap, "P_DECAY"     + juce::String(i), pDecays[i-1]);
            setParam(ap, "P_SUSTAIN"   + juce::String(i), 0.0f);
            setParam(ap, "P_RELEASE"   + juce::String(i), pDecays[i-1]);
            setParam(ap, "MOD_TARGET"  + juce::String(i), 0.0f);
            setParam(ap, "MOD_DEPTH"   + juce::String(i), 0.0f);
            setParam(ap, "MOD_RATE"    + juce::String(i), 2.0f);
            setParam(ap, "MOD_WAVEFORM"+ juce::String(i), 0.0f);
            setParam(ap, "ANIM_MODE"   + juce::String(i), 0.0f);
            setParam(ap, "ANIM_CURVE"  + juce::String(i), 0.0f);
            setParam(ap, "ANIM_DEPTH"  + juce::String(i), 1.0f);
            setParam(ap, "ANIM_DURATION"+ juce::String(i), 2.0f);
        }
        for (int i = 9; i <= 16; ++i)
            setParam(ap, "GAIN" + juce::String(i), 0.0f);

        setParam(ap, "ATTACK",      0.001f);
        setParam(ap, "DECAY",       0.60f);
        setParam(ap, "SUSTAIN",     0.0f);
        setParam(ap, "RELEASE",     0.60f);
        setParam(ap, "MASTER_GAIN", 0.52f);
        setParam(ap, "LPF_ENABLED", 1.0f);
        setParam(ap, "LPF_CUTOFF",  11000.0f);
        setParam(ap, "LPF_RESO",    1.2f);
    }

    // ------------------------------------------------------------------------
    else if (index == 10) // Sharkfin Brass - Yamaha CS-80 / OB-8 brass standard
    {
        setParam(ap, "NUM_PARTIALS", 6.0f);
        setParam(ap, "VOICE_MODE",   0.0f); // Poly
        setParam(ap, "VELOCITY_ENABLED", 1.0f);

        float ratios[6]  = { 1.0f,  2.0f,  3.0f,  4.0f,  5.0f,  6.0f };
        float gains[6]   = { 1.0f,  0.65f, 0.45f, 0.32f, 0.20f, 0.12f };
        float detunes[6] = { 0.0f,  4.0f, -4.0f,  3.0f, -3.0f,  1.5f };
        float pans[6]    = { 0.0f, -0.3f,  0.3f, -0.5f,  0.5f,  0.0f };

        for (int i = 1; i <= 6; ++i) {
            setParam(ap, "WAVEFORM"    + juce::String(i), 5.0f); // Sharkfin
            setParam(ap, "RATIO"       + juce::String(i), ratios[i-1]);
            setParam(ap, "GAIN"        + juce::String(i), gains[i-1]);
            setParam(ap, "DETUNE"      + juce::String(i), detunes[i-1]);
            setParam(ap, "PAN"         + juce::String(i), pans[i-1]);
            setParam(ap, "P_ATTACK"    + juce::String(i), 0.09f); // Horn swell
            setParam(ap, "P_DECAY"     + juce::String(i), 0.60f);
            setParam(ap, "P_SUSTAIN"   + juce::String(i), 0.75f);
            setParam(ap, "P_RELEASE"   + juce::String(i), 0.45f);
            setParam(ap, "MOD_TARGET"  + juce::String(i), 0.0f); // None
            setParam(ap, "MOD_DEPTH"   + juce::String(i), 0.0f);
            setParam(ap, "MOD_RATE"    + juce::String(i), 2.0f);
            setParam(ap, "MOD_WAVEFORM"+ juce::String(i), 0.0f);
            setParam(ap, "ANIM_MODE"   + juce::String(i), 0.0f);
            setParam(ap, "ANIM_CURVE"  + juce::String(i), 0.0f);
            setParam(ap, "ANIM_DEPTH"  + juce::String(i), 1.0f);
            setParam(ap, "ANIM_DURATION"+ juce::String(i), 2.0f);
        }
        for (int i = 7; i <= 16; ++i)
            setParam(ap, "GAIN" + juce::String(i), 0.0f);

        setParam(ap, "ATTACK",      0.09f);
        setParam(ap, "DECAY",       0.60f);
        setParam(ap, "SUSTAIN",     0.75f);
        setParam(ap, "RELEASE",     0.45f);
        setParam(ap, "MASTER_GAIN", 0.52f);
        setParam(ap, "LPF_ENABLED", 1.0f);
        setParam(ap, "LPF_CUTOFF",  2800.0f); // CS-80 brass filter bite
        setParam(ap, "LPF_RESO",    1.8f);
    }

    // ------------------------------------------------------------------------
    else if (index == 11) // Chiptune - Nintendo NES 2A03 APU standard
    {
        setParam(ap, "NUM_PARTIALS", 3.0f);
        setParam(ap, "VOICE_MODE",   0.0f); // Poly
        setParam(ap, "VELOCITY_ENABLED", 0.0f); // Authentic unweighted 8-bit chip

        // NES channels: Pulse lead (1.0x), Triangle bass (0.5x sub), Noise percussion (2.0x)
        float ratios[3]  = { 1.0f,  0.5f,  2.0f };
        float gains[3]   = { 0.95f, 0.85f, 0.22f };
        float waves[3]   = { 4.0f,  1.0f,  7.0f }; // Pulse 25%, Triangle, Noise
        float detunes[3] = { 0.0f,  0.0f,  0.0f };
        float pans[3]    = { 0.0f,  0.0f,  0.0f };

        for (int i = 1; i <= 3; ++i) {
            setParam(ap, "WAVEFORM"    + juce::String(i), waves[i-1]);
            setParam(ap, "RATIO"       + juce::String(i), ratios[i-1]);
            setParam(ap, "SEMITONE"    + juce::String(i), 0.0f);
            setParam(ap, "GAIN"        + juce::String(i), gains[i-1]);
            setParam(ap, "DETUNE"      + juce::String(i), detunes[i-1]);
            setParam(ap, "PAN"         + juce::String(i), pans[i-1]);
            setParam(ap, "P_ATTACK"    + juce::String(i), 0.001f);
            setParam(ap, "P_DECAY"     + juce::String(i), 0.12f);
            setParam(ap, "P_SUSTAIN"   + juce::String(i), 1.0f);
            setParam(ap, "P_RELEASE"   + juce::String(i), 0.02f);

            // Fast square vibrato on pulse lead
            if (i == 1) {
                setParam(ap, "MOD_TARGET"  + juce::String(i), 1.0f); // Pitch
                setParam(ap, "MOD_DEPTH"   + juce::String(i), 0.02f);
                setParam(ap, "MOD_RATE"    + juce::String(i), 12.0f);
                setParam(ap, "MOD_WAVEFORM"+ juce::String(i), 3.0f); // Square
            } else {
                setParam(ap, "MOD_TARGET"  + juce::String(i), 0.0f);
                setParam(ap, "MOD_DEPTH"   + juce::String(i), 0.0f);
            }
            setParam(ap, "ANIM_MODE"   + juce::String(i), 0.0f);
            setParam(ap, "ANIM_CURVE"  + juce::String(i), 0.0f);
            setParam(ap, "ANIM_DEPTH"  + juce::String(i), 1.0f);
            setParam(ap, "ANIM_DURATION"+ juce::String(i), 2.0f);
        }
        for (int i = 4; i <= 16; ++i)
            setParam(ap, "GAIN" + juce::String(i), 0.0f);

        setParam(ap, "ATTACK",      0.001f);
        setParam(ap, "DECAY",       0.12f);
        setParam(ap, "SUSTAIN",     1.0f);
        setParam(ap, "RELEASE",     0.02f);
        setParam(ap, "MASTER_GAIN", 0.50f);
    }

    // ------------------------------------------------------------------------
    else if (index == 12) // Wobbling Square - Dubstep wobble bass standard
    {
        setParam(ap, "NUM_PARTIALS", 4.0f);
        setParam(ap, "VOICE_MODE",   2.0f); // Legato
        setParam(ap, "GLIDE_TIME",   0.04f);
        setParam(ap, "BASE_NOTE",    48.0f); // C3
        setParam(ap, "VELOCITY_ENABLED", 1.0f);

        float ratios[4]  = { 1.0f,  3.0f,  5.0f,  7.0f }; // Odd square harmonics
        float gains[4]   = { 1.0f,  0.55f, 0.30f, 0.15f };
        float detunes[4] = { 0.0f,  3.0f, -3.0f,  2.0f };

        for (int i = 1; i <= 4; ++i) {
            setParam(ap, "WAVEFORM"    + juce::String(i), 3.0f); // Square
            setParam(ap, "RATIO"       + juce::String(i), ratios[i-1]);
            setParam(ap, "GAIN"        + juce::String(i), gains[i-1]);
            setParam(ap, "DETUNE"      + juce::String(i), detunes[i-1]);
            setParam(ap, "PAN"         + juce::String(i), 0.0f);
            setParam(ap, "P_GLIDE"     + juce::String(i), 0.04f);
            setParam(ap, "P_ATTACK"    + juce::String(i), 0.05f);
            setParam(ap, "P_DECAY"     + juce::String(i), 0.50f);
            setParam(ap, "P_SUSTAIN"   + juce::String(i), 1.0f);
            setParam(ap, "P_RELEASE"   + juce::String(i), 0.30f);
            setParam(ap, "MOD_TARGET"  + juce::String(i), 2.0f); // Level (wobble)
            setParam(ap, "MOD_DEPTH"   + juce::String(i), 0.85f);
            setParam(ap, "MOD_RATE"    + juce::String(i), 2.5f); // 2.5 Hz wobble
            setParam(ap, "MOD_WAVEFORM"+ juce::String(i), 0.0f); // Sine
            setParam(ap, "MOD_PHASE"   + juce::String(i), (float)((i - 1) * 45));
            setParam(ap, "ANIM_MODE"   + juce::String(i), 0.0f);
            setParam(ap, "ANIM_CURVE"  + juce::String(i), 0.0f);
            setParam(ap, "ANIM_DEPTH"  + juce::String(i), 1.0f);
            setParam(ap, "ANIM_DURATION"+ juce::String(i), 2.0f);
        }
        for (int i = 5; i <= 16; ++i)
            setParam(ap, "GAIN" + juce::String(i), 0.0f);

        setParam(ap, "ATTACK",      0.05f);
        setParam(ap, "DECAY",       0.50f);
        setParam(ap, "SUSTAIN",     1.0f);
        setParam(ap, "RELEASE",     0.30f);
        setParam(ap, "MASTER_GAIN", 0.54f);
        setParam(ap, "LPF_ENABLED", 1.0f);
        setParam(ap, "LPF_CUTOFF",  1800.0f);
        setParam(ap, "LPF_RESO",    3.5f);
    }

    // ------------------------------------------------------------------------
    else if (index == 13) // Too Much Glue! - extreme asynchronous overlapping multi-partial weirdness
    {
        setParam(ap, "NUM_PARTIALS", 8.0f);
        setParam(ap, "VOICE_MODE",   0.0f); // Poly
        setParam(ap, "VELOCITY_ENABLED", 1.0f);

        // Irrational/inharmonic ratios that clash and morph together
        float ratios[8]   = { 1.0f,  1.414f, 2.18f,  3.1416f, 4.67f,  6.283f, 8.53f,  11.37f };
        float gains[8]    = { 1.0f,  0.75f,  0.65f,  0.55f,   0.45f,  0.38f,  0.30f,  0.22f };
        float detunes[8]  = { 0.0f, -14.0f,  18.0f, -8.0f,    25.0f, -20.0f,  11.0f, -28.0f };
        float pans[8]     = { 0.0f, -0.65f,  0.65f, -0.40f,   0.40f, -0.85f,  0.85f,  0.0f };
        // Tri, Sharkfin, Fibonacci, Saw, Pulse, Tri, Sharkfin, Fibonacci
        float waves[8]    = { 1.0f,  5.0f,   6.0f,   2.0f,    4.0f,   1.0f,   5.0f,   6.0f };

        // Overlapping asynchronous ADSRs
        float atks[8] = { 0.02f, 0.85f, 1.40f, 0.01f, 2.10f, 0.50f, 1.10f, 1.70f };
        float decs[8] = { 0.80f, 1.40f, 2.00f, 0.45f, 1.10f, 1.80f, 0.90f, 1.30f };
        float suss[8] = { 0.90f, 0.55f, 0.75f, 0.25f, 0.70f, 0.50f, 0.40f, 0.30f };
        float rels[8] = { 1.20f, 1.80f, 2.20f, 0.60f, 2.50f, 1.50f, 2.00f, 2.80f };

        // Mod 1 (LFO) - targets: 1=Pitch, 2=Level, 3=Pan, 4=Detune
        float modTargets[8] = { 1.0f, 2.0f, 3.0f, 4.0f, 1.0f, 2.0f, 3.0f, 1.0f };
        float modRates[8]   = { 0.43f, 1.13f, 0.29f, 2.71f, 0.67f, 1.83f, 0.53f, 3.41f };
        float modDepths[8]  = { 0.07f, 0.45f, 0.75f, 0.55f, 0.05f, 0.50f, 0.85f, 0.10f };
        float modWaves[8]   = { 0.0f, 1.0f, 2.0f, 0.0f, 3.0f, 0.0f, 1.0f, 2.0f }; // Sine, Tri, Saw, Sine, Square...

        // Keyframe Animation: multi-point evolving paths for each partial
        float animModes[8]  = { 3.0f, 2.0f, 3.0f, 2.0f, 3.0f, 2.0f, 3.0f, 2.0f }; // Ping-Pong & Loop
        float animCurves[8] = { 0.0f, 0.0f, 1.0f, 2.0f, 0.0f, 1.0f, 0.0f, 2.0f }; // Smooth, Linear, Step
        float animDurs[8]   = { 2.4f, 1.8f, 3.1f, 1.5f, 2.7f, 3.8f, 1.9f, 2.2f };

        for (int i = 1; i <= 8; ++i) {
            setParam(ap, "WAVEFORM"      + juce::String(i), waves[i-1]);
            setParam(ap, "RATIO"         + juce::String(i), ratios[i-1]);
            setParam(ap, "GAIN"          + juce::String(i), gains[i-1]);
            setParam(ap, "DETUNE"        + juce::String(i), detunes[i-1]);
            setParam(ap, "PAN"           + juce::String(i), pans[i-1]);
            setParam(ap, "P_ADSR_ENABLED"+ juce::String(i), 1.0f);
            setParam(ap, "P_ATTACK"      + juce::String(i), atks[i-1]);
            setParam(ap, "P_DECAY"       + juce::String(i), decs[i-1]);
            setParam(ap, "P_SUSTAIN"     + juce::String(i), suss[i-1]);
            setParam(ap, "P_RELEASE"     + juce::String(i), rels[i-1]);
            setParam(ap, "P_GLIDE"       + juce::String(i), 0.0f);

            setParam(ap, "MOD_TARGET"    + juce::String(i), modTargets[i-1]);
            setParam(ap, "MOD_RATE"      + juce::String(i), modRates[i-1]);
            setParam(ap, "MOD_DEPTH"     + juce::String(i), modDepths[i-1]);
            setParam(ap, "MOD_WAVEFORM"  + juce::String(i), modWaves[i-1]);
            setParam(ap, "MOD_PHASE"     + juce::String(i), (float)((i * 47) % 360));

            setParam(ap, "ANIM_MODE"     + juce::String(i), animModes[i-1]);
            setParam(ap, "ANIM_CURVE"    + juce::String(i), animCurves[i-1]);
            setParam(ap, "ANIM_DURATION" + juce::String(i), animDurs[i-1]);
            setParam(ap, "ANIM_DEPTH"    + juce::String(i), 0.85f);

            PartialAnimationData track;
            float r0 = ratios[i-1];
            float g0 = gains[i-1];
            float d0 = detunes[i-1];
            float p0 = pans[i-1];
            track.keyframes[0] = { 0.0f,  r0,                                           g0,                                         0.0f,  d0,                                          p0 };
            track.keyframes[1] = { 0.35f, juce::jlimit(0.1f, 32.0f, r0 * 1.18f),        juce::jlimit(0.05f, 1.0f, g0 * 0.65f),       2.0f,  juce::jlimit(-50.0f, 50.0f, -d0 * 0.8f),     -p0 };
            track.keyframes[2] = { 0.70f, juce::jlimit(0.1f, 32.0f, r0 * 0.84f),        juce::jlimit(0.05f, 1.0f, g0 * 1.15f),      -3.0f,  juce::jlimit(-50.0f, 50.0f, d0 * 0.5f),       p0 * 0.5f };
            track.keyframes[3] = { 1.0f,  r0,                                           g0,                                         0.0f,  d0,                                          p0 };
            track.numKeyframes = 4;
            audioProcessor.setPartialAnimation(i - 1, track);
        }
        for (int i = 9; i <= 16; ++i)
            setParam(ap, "GAIN" + juce::String(i), 0.0f);

        setParam(ap, "ATTACK",      0.05f);
        setParam(ap, "DECAY",       1.50f);
        setParam(ap, "SUSTAIN",     0.85f);
        setParam(ap, "RELEASE",     2.50f);
        setParam(ap, "MASTER_GAIN", 0.50f);
        setParam(ap, "HPF_ENABLED", 1.0f);
        setParam(ap, "HPF_CUTOFF",  35.0f);
        setParam(ap, "HPF_RESO",    0.707f);
        setParam(ap, "LPF_ENABLED", 1.0f);
        setParam(ap, "LPF_CUTOFF",  4200.0f);
        setParam(ap, "LPF_RESO",    2.8f);
    }

    // ------------------------------------------------------------------------
    else if (index == 14) // Alien Drone - restored to 4 partials (classic inharmonic saw drone)
    {
        // Reverted to exactly 4 partials as preferred by user
        setParam(ap, "NUM_PARTIALS", 4.0f);
        setParam(ap, "VOICE_MODE",   0.0f); // Poly
        setParam(ap, "VELOCITY_ENABLED", 1.0f);

        // Classic Inharmonic / Bell harmonic series: 1.0, 1.5, 2.0, 2.5
        float ratios[4]  = { 1.0f,  1.5f,  2.0f,  2.5f };
        float gains[4]   = { 1.0f,  0.50f, 0.50f, 0.50f }; // Equal/flat presence
        float detunes[4] = { 0.0f,  4.0f, -4.0f,  6.0f }; // Wide detune spread
        float pans[4]    = { 0.0f, -0.45f, 0.45f, 0.0f };

        for (int i = 1; i <= 4; ++i) {
            setParam(ap, "WAVEFORM"    + juce::String(i), 2.0f); // Raw Sawtooth
            setParam(ap, "RATIO"       + juce::String(i), ratios[i-1]);
            setParam(ap, "GAIN"        + juce::String(i), gains[i-1]);
            setParam(ap, "DETUNE"      + juce::String(i), detunes[i-1]);
            setParam(ap, "PAN"         + juce::String(i), pans[i-1]);
            setParam(ap, "P_ATTACK"    + juce::String(i), 2.0f);
            setParam(ap, "P_DECAY"     + juce::String(i), 1.0f);
            setParam(ap, "P_SUSTAIN"   + juce::String(i), 1.0f);
            setParam(ap, "P_RELEASE"   + juce::String(i), 3.5f);
            setParam(ap, "P_GLIDE"     + juce::String(i), 0.0f);

            // Staggered eerie pitch drift: 0.2 + i * 0.1, 45-degree phase steps
            setParam(ap, "MOD_TARGET"  + juce::String(i), 1.0f); // Pitch
            setParam(ap, "MOD_DEPTH"   + juce::String(i), 0.08f);
            setParam(ap, "MOD_RATE"    + juce::String(i), 0.2f + (float)i * 0.1f);
            setParam(ap, "MOD_WAVEFORM"+ juce::String(i), 0.0f); // Sine
            setParam(ap, "MOD_PHASE"   + juce::String(i), (float)((i * 45) % 360));

            // Keyframe Animation: Off by default unless enabled by the user
            setParam(ap, "ANIM_MODE"     + juce::String(i), 0.0f);
            setParam(ap, "ANIM_CURVE"    + juce::String(i), 0.0f);
            setParam(ap, "ANIM_DEPTH"    + juce::String(i), 1.0f);
            setParam(ap, "ANIM_DURATION" + juce::String(i), 2.0f);
        }
        for (int i = 5; i <= 16; ++i)
            setParam(ap, "GAIN" + juce::String(i), 0.0f);

        setParam(ap, "ATTACK",      2.0f);
        setParam(ap, "DECAY",       1.0f);
        setParam(ap, "SUSTAIN",     1.0f);
        setParam(ap, "RELEASE",     3.5f);
        setParam(ap, "MASTER_GAIN", 0.54f);
        setParam(ap, "HPF_ENABLED", 1.0f);
        setParam(ap, "HPF_CUTOFF",  35.0f); // Cleans sub-bass rumble
        setParam(ap, "HPF_RESO",    0.707f);
    }

    // ------------------------------------------------------------------------
    else if (index == 15) // Soft Pad - warm analog poly-pad standard
    {
        setParam(ap, "NUM_PARTIALS", 6.0f);
        setParam(ap, "VOICE_MODE",   0.0f); // Poly
        setParam(ap, "VELOCITY_ENABLED", 1.0f);

        float ratios[6]  = { 1.0f,  2.0f,  3.0f,  4.0f,  5.0f,  6.0f };
        float gains[6]   = { 1.0f,  0.65f, 0.40f, 0.25f, 0.15f, 0.08f };
        float detunes[6] = { 0.0f,  3.5f, -3.5f,  2.0f, -2.0f,  1.0f };
        float pans[6]    = { 0.0f, -0.4f,  0.4f, -0.6f,  0.6f,  0.0f };
        float waves[6]   = { 0.0f,  1.0f,  0.0f,  1.0f,  0.0f,  1.0f }; // Sine & Triangle blend

        for (int i = 1; i <= 6; ++i) {
            setParam(ap, "WAVEFORM"    + juce::String(i), waves[i-1]);
            setParam(ap, "RATIO"       + juce::String(i), ratios[i-1]);
            setParam(ap, "GAIN"        + juce::String(i), gains[i-1]);
            setParam(ap, "DETUNE"      + juce::String(i), detunes[i-1]);
            setParam(ap, "PAN"         + juce::String(i), pans[i-1]);
            setParam(ap, "P_ATTACK"    + juce::String(i), 0.65f);
            setParam(ap, "P_DECAY"     + juce::String(i), 1.8f);
            setParam(ap, "P_SUSTAIN"   + juce::String(i), 0.82f);
            setParam(ap, "P_RELEASE"   + juce::String(i), 1.8f);
            setParam(ap, "P_GLIDE"     + juce::String(i), 0.0f);
            setParam(ap, "MOD_TARGET"  + juce::String(i), 1.0f); // Pitch chorus
            setParam(ap, "MOD_DEPTH"   + juce::String(i), 0.012f);
            setParam(ap, "MOD_RATE"    + juce::String(i), 0.50f);
            setParam(ap, "MOD_WAVEFORM"+ juce::String(i), 0.0f); // Sine
            setParam(ap, "MOD_PHASE"   + juce::String(i), (float)((i * 45) % 360));
            setParam(ap, "ANIM_MODE"   + juce::String(i), 0.0f);
            setParam(ap, "ANIM_CURVE"  + juce::String(i), 0.0f);
            setParam(ap, "ANIM_DEPTH"  + juce::String(i), 1.0f);
            setParam(ap, "ANIM_DURATION"+ juce::String(i), 2.0f);
        }
        for (int i = 7; i <= 16; ++i)
            setParam(ap, "GAIN" + juce::String(i), 0.0f);

        setParam(ap, "ATTACK",      0.65f);
        setParam(ap, "DECAY",       1.8f);
        setParam(ap, "SUSTAIN",     0.82f);
        setParam(ap, "RELEASE",     1.8f);
        setParam(ap, "MASTER_GAIN", 0.52f);
        setParam(ap, "LPF_ENABLED", 1.0f);
        setParam(ap, "LPF_CUTOFF",  3600.0f);
        setParam(ap, "LPF_RESO",    0.707f);
    }

    // ------------------------------------------------------------------------
    else if (index == 16) // Seraphim - Angelic celestial choir pad, warm sub foundation
    {
        setParam(ap, "NUM_PARTIALS", 5.0f);
        setParam(ap, "VOICE_MODE",   0.0f); // Poly
        setParam(ap, "VELOCITY_ENABLED", 1.0f);

        // Partial 1: Base note at 0.5x ratio (deep angelic sub foundation)
        setParam(ap, "WAVEFORM1",     0.0f); // Pure Sine
        setParam(ap, "RATIO1",        0.5f); // Base note at 0.5x ratio
        setParam(ap, "SEMITONE1",     0.0f);
        setParam(ap, "GAIN1",         0.95f);
        setParam(ap, "DETUNE1",       0.0f);
        setParam(ap, "PAN1",          0.0f);
        setParam(ap, "MOD_TARGET1",   2.0f); // Gentle breathing amplitude swell
        setParam(ap, "MOD_RATE1",     0.22f); // Slow celestial swell (~4.5s cycle)
        setParam(ap, "MOD_DEPTH1",    0.15f);
        setParam(ap, "MOD_WAVEFORM1", 0.0f); // Sine
        setParam(ap, "MOD_PHASE1",    0.0f);
        setParam(ap, "ANIM_MODE1",    0.0f);
        setParam(ap, "P_ATTACK1",     0.80f);
        setParam(ap, "P_DECAY1",      2.5f);
        setParam(ap, "P_SUSTAIN1",    0.85f);
        setParam(ap, "P_RELEASE1",    3.6f);

        // Partial 2: Angelic principal octave (1.0x) with sweet chorused detune
        setParam(ap, "WAVEFORM2",     0.0f); // Pure Sine
        setParam(ap, "RATIO2",        1.0f); // 1.0x fundamental
        setParam(ap, "SEMITONE2",     0.0f);
        setParam(ap, "GAIN2",         0.80f);
        setParam(ap, "DETUNE2",       3.5f);
        setParam(ap, "PAN2",         -0.30f);
        setParam(ap, "MOD_TARGET2",   1.0f); // Angelic choir vibrato
        setParam(ap, "MOD_RATE2",     4.2f); // Subtle 4.2 Hz vibrato
        setParam(ap, "MOD_DEPTH2",    0.010f); // Delicate depth
        setParam(ap, "MOD_WAVEFORM2", 0.0f); // Sine
        setParam(ap, "MOD_PHASE2",    45.0f);
        setParam(ap, "ANIM_MODE2",    0.0f);
        setParam(ap, "P_ATTACK2",     0.65f);
        setParam(ap, "P_DECAY2",      2.5f);
        setParam(ap, "P_SUSTAIN2",    0.85f);
        setParam(ap, "P_RELEASE2",    3.6f);

        // Partial 3: Heavenly fifth (1.5x) with slow stereo drift
        setParam(ap, "WAVEFORM3",     1.0f); // Triangle (soft warm harmonics)
        setParam(ap, "RATIO3",        1.5f); // Perfect fifth above fundamental
        setParam(ap, "SEMITONE3",     0.0f);
        setParam(ap, "GAIN3",         0.65f);
        setParam(ap, "DETUNE3",      -3.0f);
        setParam(ap, "PAN3",          0.30f);
        setParam(ap, "MOD_TARGET3",   3.0f); // Pan modulation
        setParam(ap, "MOD_RATE3",     0.15f); // Very slow drift
        setParam(ap, "MOD_DEPTH3",    0.65f); // Gentle ear-to-ear floating
        setParam(ap, "MOD_WAVEFORM3", 0.0f); // Sine
        setParam(ap, "MOD_PHASE3",    90.0f);
        setParam(ap, "ANIM_MODE3",    0.0f);
        setParam(ap, "P_ATTACK3",     0.90f);
        setParam(ap, "P_DECAY3",      2.8f);
        setParam(ap, "P_SUSTAIN3",    0.80f);
        setParam(ap, "P_RELEASE3",    3.8f);

        // Partial 4: Angelic super-octave shimmer (2.0x)
        setParam(ap, "WAVEFORM4",     0.0f); // Pure Sine
        setParam(ap, "RATIO4",        2.0f); // Super-octave
        setParam(ap, "SEMITONE4",     0.0f);
        setParam(ap, "GAIN4",         0.45f);
        setParam(ap, "DETUNE4",       5.0f);
        setParam(ap, "PAN4",         -0.45f);
        setParam(ap, "MOD_TARGET4",   1.0f); // Shimmer pitch movement
        setParam(ap, "MOD_RATE4",     5.0f);
        setParam(ap, "MOD_DEPTH4",    0.008f);
        setParam(ap, "MOD_WAVEFORM4", 0.0f);
        setParam(ap, "MOD_PHASE4",    180.0f);
        setParam(ap, "ANIM_MODE4",    0.0f);
        setParam(ap, "P_ATTACK4",     1.10f);
        setParam(ap, "P_DECAY4",      3.0f);
        setParam(ap, "P_SUSTAIN4",    0.75f);
        setParam(ap, "P_RELEASE4",    4.0f);

        // Partial 5: Celestial halo sheen (3.0x)
        setParam(ap, "WAVEFORM5",     6.0f); // Fibonacci (delicate organic sheen)
        setParam(ap, "RATIO5",        3.0f); // High celestial harmonic
        setParam(ap, "SEMITONE5",     0.0f);
        setParam(ap, "GAIN5",         0.30f);
        setParam(ap, "DETUNE5",      -4.0f);
        setParam(ap, "PAN5",          0.45f);
        setParam(ap, "MOD_TARGET5",   2.0f); // Subtle shimmer breathing
        setParam(ap, "MOD_RATE5",     0.35f);
        setParam(ap, "MOD_DEPTH5",    0.20f);
        setParam(ap, "MOD_WAVEFORM5", 0.0f);
        setParam(ap, "MOD_PHASE5",    270.0f);
        setParam(ap, "ANIM_MODE5",    0.0f);
        setParam(ap, "P_ATTACK5",     1.30f);
        setParam(ap, "P_DECAY5",      3.2f);
        setParam(ap, "P_SUSTAIN5",    0.70f);
        setParam(ap, "P_RELEASE5",    4.2f);

        for (int i = 6; i <= 16; ++i)
            setParam(ap, "GAIN" + juce::String(i), 0.0f);

        setParam(ap, "ATTACK",      0.80f);
        setParam(ap, "DECAY",       2.5f);
        setParam(ap, "SUSTAIN",     0.85f);
        setParam(ap, "RELEASE",     3.8f);
        setParam(ap, "MASTER_GAIN", 0.50f);
        // Silky airy high-end filter, subtle rumble filter
        setParam(ap, "HPF_ENABLED", 1.0f);
        setParam(ap, "HPF_CUTOFF",  30.0f);
        setParam(ap, "HPF_RESO",    0.707f);
        setParam(ap, "LPF_ENABLED", 1.0f);
        setParam(ap, "LPF_CUTOFF",  7500.0f);
        setParam(ap, "LPF_RESO",    0.707f);
    }

    // ------------------------------------------------------------------------
    else if (index == 17) // 808 Drum Bass - Classic punchy analog sub kick & booming gliding bass
    {
        setParam(ap, "NUM_PARTIALS", 3.0f);
        setParam(ap, "VOICE_MODE",   1.0f); // Mono (classic 808 mono bass)
        setParam(ap, "GLIDE_TIME",   0.035f); // Smooth pitch slides between notes
        setParam(ap, "BASE_NOTE",    36.0f); // C2 sub foundation
        setParam(ap, "VELOCITY_ENABLED", 1.0f);

        // Clear all partial animation tracks first
        audioProcessor.clearAllKeyframes();

        // Partial 1: The Sub Boom + Pitch-drop Kick transient
        setParam(ap, "WAVEFORM1",     0.0f); // Pure Sine
        setParam(ap, "RATIO1",        1.0f); // Sub Fundamental
        setParam(ap, "SEMITONE1",     0.0f);
        setParam(ap, "GAIN1",         1.0f); // Full power
        setParam(ap, "DETUNE1",       0.0f);
        setParam(ap, "PAN1",          0.0f);
        setParam(ap, "MOD_TARGET1",   0.0f); // No LFO
        setParam(ap, "P_ATTACK1",     0.002f); // Snappy instant attack
        setParam(ap, "P_DECAY1",      2.2f);   // Long booming decay
        setParam(ap, "P_SUSTAIN1",    0.18f);  // Sustained body
        setParam(ap, "P_RELEASE1",    0.35f);

        // Keyframe Animation on Partial 1: Pitch sweep attack (+18 semitones -> 0 semitones in 0.065s)
        setParam(ap, "ANIM_MODE1",     2.0f); // Time: One-Shot
        setParam(ap, "ANIM_DURATION1", 0.065f); // Fast punchy transient drop
        setParam(ap, "ANIM_CURVE1",    0.0f); // Smooth curve
        setParam(ap, "ANIM_DEPTH1",    1.0f);

        PartialKeyframe kf0;
        kf0.time = 0.0f;
        kf0.ratio = 1.0f;
        kf0.gain = 1.0f;
        kf0.semitone = 18.0f; // High punch at start of hit
        kf0.detune = 0.0f;
        kf0.pan = 0.0f;

        PartialKeyframe kf1;
        kf1.time = 1.0f;
        kf1.ratio = 1.0f;
        kf1.gain = 1.0f;
        kf1.semitone = 0.0f; // Drops to root pitch
        kf1.detune = 0.0f;
        kf1.pan = 0.0f;

        audioProcessor.addOrUpdateKeyframe(0, kf0, 0.001f);
        audioProcessor.addOrUpdateKeyframe(0, kf1, 0.001f);

        // Partial 2: 2nd Harmonic (Octave 2.0x) - Saturation / speaker body
        setParam(ap, "WAVEFORM2",     0.0f); // Pure Sine
        setParam(ap, "RATIO2",        2.0f); // Octave overtone
        setParam(ap, "SEMITONE2",     0.0f);
        setParam(ap, "GAIN2",         0.28f);
        setParam(ap, "DETUNE2",       0.0f);
        setParam(ap, "PAN2",          0.0f);
        setParam(ap, "MOD_TARGET2",   0.0f);
        setParam(ap, "ANIM_MODE2",    0.0f);
        setParam(ap, "P_ATTACK2",     0.002f);
        setParam(ap, "P_DECAY2",      0.55f); // Decays quickly, giving punch then leaving clean sub
        setParam(ap, "P_SUSTAIN2",    0.04f);
        setParam(ap, "P_RELEASE2",    0.20f);

        // Partial 3: 3rd Harmonic (3.0x) - Transient beater click
        setParam(ap, "WAVEFORM3",     0.0f); // Sine
        setParam(ap, "RATIO3",        3.0f); // 3rd harmonic
        setParam(ap, "SEMITONE3",     0.0f);
        setParam(ap, "GAIN3",         0.14f);
        setParam(ap, "DETUNE3",       0.0f);
        setParam(ap, "PAN3",          0.0f);
        setParam(ap, "MOD_TARGET3",   0.0f);
        setParam(ap, "ANIM_MODE3",    0.0f);
        setParam(ap, "P_ATTACK3",     0.001f);
        setParam(ap, "P_DECAY3",      0.10f); // Quick transient click
        setParam(ap, "P_SUSTAIN3",    0.0f);
        setParam(ap, "P_RELEASE3",    0.08f);

        for (int i = 4; i <= 16; ++i)
        {
            setParam(ap, "GAIN" + juce::String(i), 0.0f);
            setParam(ap, "ANIM_MODE" + juce::String(i), 0.0f);
        }

        // Master Envelope & Output
        setParam(ap, "ATTACK",      0.002f);
        setParam(ap, "DECAY",       2.0f);
        setParam(ap, "SUSTAIN",     0.18f);
        setParam(ap, "RELEASE",     0.35f);
        setParam(ap, "MASTER_GAIN", 0.58f);

        // Warm analog low-pass filtering, sub clean high-pass
        setParam(ap, "HPF_ENABLED", 1.0f);
        setParam(ap, "HPF_CUTOFF",  24.0f); // Clean DC rumble below 24Hz
        setParam(ap, "HPF_RESO",    0.707f);
        setParam(ap, "LPF_ENABLED", 1.0f);
        setParam(ap, "LPF_CUTOFF",  5000.0f); // Warm rounded top
        setParam(ap, "LPF_RESO",    0.707f);
    }

    // ------------------------------------------------------------------------
    else if (index == 18) // Random - procedurally generated patch
    {
        // Random number of partials between 4 and 8
        int count = 4 + (std::rand() % 5);
        setParam(ap, "NUM_PARTIALS", (float)count);
        setParam(ap, "VELOCITY_ENABLED", 1.0f);

        // Pick an archetype: 0 = Pluck, 1 = Pad, 2 = Lead, 3 = Drone
        int archetype = std::rand() % 4;
        float masterAtk = 0.01f, masterDec = 0.5f, masterSus = 0.5f, masterRel = 0.5f;

        if (archetype == 0) // Pluck
        {
            masterAtk = 0.002f;
            masterDec = 0.3f + ((float)std::rand() / RAND_MAX * 0.5f);
            masterSus = 0.0f;
            masterRel = 0.4f;
            setParam(ap, "VOICE_MODE",  0.0f); // Poly
            setParam(ap, "LPF_ENABLED", 1.0f);
            setParam(ap, "LPF_CUTOFF",  4000.0f + ((float)std::rand() / RAND_MAX * 6000.0f));
            setParam(ap, "LPF_RESO",    1.2f);
        }
        else if (archetype == 1) // Pad
        {
            masterAtk = 0.6f + ((float)std::rand() / RAND_MAX * 1.0f);
            masterDec = 2.0f;
            masterSus = 0.85f;
            masterRel = 2.0f;
            setParam(ap, "VOICE_MODE",  0.0f); // Poly
            setParam(ap, "HPF_ENABLED", 1.0f);
            setParam(ap, "HPF_CUTOFF",  50.0f);
            setParam(ap, "HPF_RESO",    0.707f);
            setParam(ap, "LPF_ENABLED", 1.0f);
            setParam(ap, "LPF_CUTOFF",  4500.0f + ((float)std::rand() / RAND_MAX * 4000.0f));
            setParam(ap, "LPF_RESO",    0.707f);
        }
        else if (archetype == 2) // Lead
        {
            masterAtk = 0.01f + ((float)std::rand() / RAND_MAX * 0.04f);
            masterDec = 0.4f;
            masterSus = 0.65f;
            masterRel = 0.35f;
            setParam(ap, "VOICE_MODE", 1.0f); // Mono
            setParam(ap, "GLIDE_TIME", 0.05f);
            setParam(ap, "LPF_ENABLED", 1.0f);
            setParam(ap, "LPF_CUTOFF", 2500.0f + ((float)std::rand() / RAND_MAX * 3500.0f));
            setParam(ap, "LPF_RESO",   2.0f);
        }
        else // Drone
        {
            masterAtk = 1.8f;
            masterDec = 2.0f;
            masterSus = 1.0f;
            masterRel = 3.0f;
            setParam(ap, "VOICE_MODE",  0.0f);
            setParam(ap, "HPF_ENABLED", 1.0f);
            setParam(ap, "HPF_CUTOFF",  35.0f);
            setParam(ap, "HPF_RESO",    0.707f);
        }

        setParam(ap, "ATTACK",      masterAtk);
        setParam(ap, "DECAY",       masterDec);
        setParam(ap, "SUSTAIN",     masterSus);
        setParam(ap, "RELEASE",     masterRel);
        setParam(ap, "MASTER_GAIN", 0.50f);

        for (int i = 1; i <= count; ++i)
        {
            float ratio = (archetype == 3) ? (1.0f + 0.5f * (i - 1)) : (float)i;
            int wave = (archetype == 1) ? (std::rand() % 2) : (std::rand() % 5);
            float baseGain = (i == 1) ? 1.0f : (1.0f / (float)i);
            float detune = ((float)std::rand() / RAND_MAX * 8.0f) - 4.0f;
            float pan = (i % 2 == 0 ? 0.35f : -0.35f) * ((float)i / (float)count);

            setParam(ap, "WAVEFORM"    + juce::String(i), (float)wave);
            setParam(ap, "RATIO"       + juce::String(i), ratio);
            setParam(ap, "GAIN"        + juce::String(i), baseGain);
            setParam(ap, "DETUNE"      + juce::String(i), detune);
            setParam(ap, "PAN"         + juce::String(i), pan);
            setParam(ap, "P_ATTACK"    + juce::String(i), masterAtk);
            setParam(ap, "P_DECAY"     + juce::String(i), masterDec);
            setParam(ap, "P_SUSTAIN"   + juce::String(i), masterSus);
            setParam(ap, "P_RELEASE"   + juce::String(i), masterRel);

            if (std::rand() % 2 == 1) {
                int modTarget = 1 + (std::rand() % 2);
                setParam(ap, "MOD_TARGET"  + juce::String(i), (float)modTarget);
                setParam(ap, "MOD_DEPTH"   + juce::String(i), modTarget == 1 ? 0.015f : 0.25f);
                setParam(ap, "MOD_RATE"    + juce::String(i), 0.5f + ((float)std::rand() / RAND_MAX * 3.0f));
                setParam(ap, "MOD_WAVEFORM"+ juce::String(i), 0.0f);
                setParam(ap, "MOD_PHASE"   + juce::String(i), (float)((i * 45) % 360));
            } else {
                setParam(ap, "MOD_TARGET"  + juce::String(i), 0.0f);
                setParam(ap, "MOD_DEPTH"   + juce::String(i), 0.0f);
            }
            setParam(ap, "ANIM_MODE"   + juce::String(i), 0.0f);
            setParam(ap, "ANIM_CURVE"  + juce::String(i), 0.0f);
            setParam(ap, "ANIM_DEPTH"  + juce::String(i), 1.0f);
            setParam(ap, "ANIM_DURATION"+ juce::String(i), 2.0f);
        }

        for (int i = count + 1; i <= 16; ++i)
            setParam(ap, "GAIN" + juce::String(i), 0.0f);
    }

    // Sync partial enabled status based on active count and gains
    int activeNum = (int)ap.getRawParameterValue("NUM_PARTIALS")->load();
    for (int i = 1; i <= 16; ++i)
    {
        float g = 0.0f;
        if (auto* gp = ap.getRawParameterValue("GAIN" + juce::String(i)))
            g = gp->load();
        bool enable = (i <= activeNum) && (g > 0.001f || i == 1);
        setParam(ap, "P_ENABLED" + juce::String(i), enable ? 1.0f : 0.0f);
    }

    partialsContainer.setActiveCount(activeNum);
    partialsContainer.refreshAllStrips();
    updateVoiceDesc();

    spectrograph.syncTimelineControlsToSelectedPartial();
    spectrograph.repaint();
    oscilloscope.repaint();

    recordPresetSnapshot();

    juce::Component::SafePointer<WaveSlaveAudioProcessorEditor> safeThis(this);
    juce::Timer::callAfterDelay(250, [safeThis]() {
        if (safeThis != nullptr)
        {
            safeThis->recordPresetSnapshot();
            safeThis->isApplyingPreset = false;
        }
    });

    if (index == 1) // Reset
    {
        factoryPresetBox.setTextWhenNothingSelected("Presets");
        factoryPresetBox.setSelectedId(0, juce::dontSendNotification);
    }
    else if (index == 18) // Random
    {
        factoryPresetBox.setTextWhenNothingSelected("Random");
        factoryPresetBox.setSelectedId(0, juce::dontSendNotification);
    }
}

void WaveSlaveAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    // Top Header row across full width
    auto headerArea = area.removeFromTop(34).reduced(10, 3);
    titleLabel.setBounds(headerArea.removeFromLeft(160));

    // Header dropdowns (Right-to-left: Partials, Presets, Edit, File)
    partialsComboBox.setBounds(headerArea.removeFromRight(115).reduced(2, 2));
    headerArea.removeFromRight(4);
    factoryPresetBox.setBounds(headerArea.removeFromRight(105).reduced(2, 2));
    headerArea.removeFromRight(4);
    editMenuBox.setBounds(headerArea.removeFromRight(70).reduced(2, 2));
    headerArea.removeFromRight(4);
    fileMenuBox.setBounds(headerArea.removeFromRight(72).reduced(2, 2));
    headerArea.removeFromRight(8);

    // Main workspace below header
    auto mainWorkspace = area.reduced(10);

    // Sidebar tool docked on the left
    auto sidebarArea = mainWorkspace.removeFromLeft(52);
    sideBar.setBounds(sidebarArea);
    mainWorkspace.removeFromLeft(8); // Spacing between sidebar and content

    // Keyframe Timeline Panel docked at the bottom of the screen (independent of the graph)
    timelineDock.setVisible(isTimelineOpen);
    if (isTimelineOpen)
    {
        auto tlArea = mainWorkspace.removeFromBottom(76);
        mainWorkspace.removeFromBottom(8);
        timelineDock.setBounds(tlArea.reduced(3));
    }

    int numPartials = partialsContainer.getActiveCount();
    bool showSpectrograph = isGraphsOpen && isSpecOpen;
    bool showScope = isGraphsOpen && isScopeOpen;

    spectrograph.setVisible(showSpectrograph);
    oscilloscope.setVisible(showScope);

    auto layoutMasterControls = [&](juce::Rectangle<int> masterArea, bool isTallMode)
    {
        if (isTallMode)
        {
            // Tall mode (alongside Oscilloscope): 200px height
            // We have ~390-450px width and 200px height.
            // Split into 2 wide columns instead of 3 squished ones:
            // Left Column: OUTPUT (top) and VOICE (bottom) stacked vertically -> completely eliminates horizontal squishing!
            // Right Column: MAIN ENVELOPE (Attack, Decay, Sustain, Release)
            int colSpacing = 16;
            int totalW = masterArea.getWidth();
            int leftColW = juce::jmax(150, juce::jmin(180, (totalW - colSpacing) / 3));

            // Left Column: OUTPUT
            auto leftCol = masterArea.removeFromLeft(leftColW);
            masterArea.removeFromLeft(colSpacing);

            // Center vertically within 200px height (Output takes ~74px)
            int leftPadY = juce::jmax(0, (masterArea.getHeight() - 74) / 2);
            leftCol.removeFromTop(leftPadY);

            // Group 1: OUTPUT
            outputLabel.setBounds(leftCol.removeFromTop(16));
            leftCol.removeFromTop(4);
            masterGainSlider.setBounds(leftCol.removeFromTop(24));
            leftCol.removeFromTop(6);
            masterPanSlider.setBounds(leftCol.removeFromTop(24));

            // Right Column: MAIN ENVELOPE, VOICE, or FILTER CONTROLS
            auto eCol = masterArea;
            int rightPadY = juce::jmax(0, (masterArea.getHeight() - 138) / 2);
            eCol.removeFromTop(rightPadY);

            // Hide all by default
            mainEnvLabel.setVisible(false);
            attackVSlider.setVisible(false);
            decayVSlider.setVisible(false);
            sustainVSlider.setVisible(false);
            releaseVSlider.setVisible(false);

            lpfSectionLabel.setVisible(false);
            lpfEnableButton.setVisible(false);
            lpfCutoffSlider.setVisible(false);
            lpfResoSlider.setVisible(false);
            hpfSectionLabel.setVisible(false);
            hpfEnableButton.setVisible(false);
            hpfCutoffSlider.setVisible(false);
            hpfResoSlider.setVisible(false);

            voiceSectionLabel.setVisible(false);
            masterVoiceBox.setVisible(false);
            masterGlideSlider.setVisible(false);
            voiceModeDescLabel.setVisible(false);

            if (currentMasterPanel == SideBarTool::MasterPanel::Envelope)
            {
                mainEnvLabel.setVisible(true);
                attackVSlider.setVisible(true);
                decayVSlider.setVisible(true);
                sustainVSlider.setVisible(true);
                releaseVSlider.setVisible(true);

                mainEnvLabel.setBounds(eCol.removeFromTop(16));
                eCol.removeFromTop(4);
                int eSliderH = 22;
                int eGap = 6;
                attackVSlider.setBounds(eCol.removeFromTop(eSliderH));
                eCol.removeFromTop(eGap);
                decayVSlider.setBounds(eCol.removeFromTop(eSliderH));
                eCol.removeFromTop(eGap);
                sustainVSlider.setBounds(eCol.removeFromTop(eSliderH));
                eCol.removeFromTop(eGap);
                releaseVSlider.setBounds(eCol.removeFromTop(eSliderH));
            }
            else if (currentMasterPanel == SideBarTool::MasterPanel::Voice)
            {
                voiceSectionLabel.setVisible(true);
                masterVoiceBox.setVisible(true);
                voiceModeDescLabel.setVisible(true);

                voiceSectionLabel.setBounds(eCol.removeFromTop(16));
                eCol.removeFromTop(6);
                masterVoiceBox.setBounds(eCol.removeFromTop(24).reduced(0, 1));
                eCol.removeFromTop(6);

                bool isPoly = (masterVoiceBox.getSelectedId() <= 1);
                if (!isPoly)
                {
                    masterGlideSlider.setVisible(true);
                    masterGlideSlider.setBounds(eCol.removeFromTop(24));
                    eCol.removeFromTop(8);
                }
                else
                {
                    masterGlideSlider.setVisible(false);
                }
                voiceModeDescLabel.setBounds(eCol.removeFromTop(36));
            }
            else // Filters
            {
                lpfSectionLabel.setVisible(true);
                lpfEnableButton.setVisible(true);
                lpfCutoffSlider.setVisible(true);
                lpfResoSlider.setVisible(true);
                hpfSectionLabel.setVisible(true);
                hpfEnableButton.setVisible(true);
                hpfCutoffSlider.setVisible(true);
                hpfResoSlider.setVisible(true);

                int halfH = (eCol.getHeight() - 6) / 2;
                auto lpfArea = eCol.removeFromTop(halfH);
                eCol.removeFromTop(6);
                auto hpfArea = eCol;

                auto lHeader = lpfArea.removeFromTop(16);
                lpfEnableButton.setBounds(lHeader.removeFromRight(32).reduced(0, 1));
                lHeader.removeFromRight(4);
                lpfSectionLabel.setBounds(lHeader);
                lpfArea.removeFromTop(2);
                lpfCutoffSlider.setBounds(lpfArea.removeFromTop(20));
                lpfArea.removeFromTop(2);
                lpfResoSlider.setBounds(lpfArea.removeFromTop(20));

                auto hHeader = hpfArea.removeFromTop(16);
                hpfEnableButton.setBounds(hHeader.removeFromRight(32).reduced(0, 1));
                hHeader.removeFromRight(4);
                hpfSectionLabel.setBounds(hHeader);
                hpfArea.removeFromTop(2);
                hpfCutoffSlider.setBounds(hpfArea.removeFromTop(20));
                hpfArea.removeFromTop(2);
                hpfResoSlider.setBounds(hpfArea.removeFromTop(20));
            }
        }
        else
        {
            // Wide mode (full width bottom bar): 84px height
            int colSpacing = 16;
            int totalW = masterArea.getWidth();
            int outW = juce::jmax(160, juce::jmin(220, (totalW * 20) / 100));

            // Group 1: OUTPUT
            auto outCol = masterArea.removeFromLeft(outW);
            masterArea.removeFromLeft(colSpacing);
            outputLabel.setBounds(outCol.removeFromTop(16));
            outCol.removeFromTop(2);
            masterGainSlider.setBounds(outCol.removeFromTop(24));
            outCol.removeFromTop(4);
            masterPanSlider.setBounds(outCol.removeFromTop(24));

            // Group 2: ENVELOPE, VOICE, OR FILTERS
            auto eCol = masterArea;

            mainEnvLabel.setVisible(false);
            attackVSlider.setVisible(false);
            decayVSlider.setVisible(false);
            sustainVSlider.setVisible(false);
            releaseVSlider.setVisible(false);

            lpfSectionLabel.setVisible(false);
            lpfEnableButton.setVisible(false);
            lpfCutoffSlider.setVisible(false);
            lpfResoSlider.setVisible(false);
            hpfSectionLabel.setVisible(false);
            hpfEnableButton.setVisible(false);
            hpfCutoffSlider.setVisible(false);
            hpfResoSlider.setVisible(false);

            voiceSectionLabel.setVisible(false);
            masterVoiceBox.setVisible(false);
            masterGlideSlider.setVisible(false);
            voiceModeDescLabel.setVisible(false);

            if (currentMasterPanel == SideBarTool::MasterPanel::Envelope)
            {
                mainEnvLabel.setVisible(true);
                attackVSlider.setVisible(true);
                decayVSlider.setVisible(true);
                sustainVSlider.setVisible(true);
                releaseVSlider.setVisible(true);

                mainEnvLabel.setBounds(eCol.removeFromTop(16));
                eCol.removeFromTop(2);
                int numSliders = 4;
                int sp = 8;
                int sliderW = (eCol.getWidth() - sp * (numSliders - 1)) / numSliders;
                attackVSlider.setBounds(eCol.removeFromLeft(sliderW));
                eCol.removeFromLeft(sp);
                decayVSlider.setBounds(eCol.removeFromLeft(sliderW));
                eCol.removeFromLeft(sp);
                sustainVSlider.setBounds(eCol.removeFromLeft(sliderW));
                eCol.removeFromLeft(sp);
                releaseVSlider.setBounds(eCol);
            }
            else if (currentMasterPanel == SideBarTool::MasterPanel::Voice)
            {
                voiceSectionLabel.setVisible(true);
                masterVoiceBox.setVisible(true);
                voiceModeDescLabel.setVisible(true);

                voiceSectionLabel.setBounds(eCol.removeFromTop(16));
                eCol.removeFromTop(8);

                auto row = eCol.removeFromTop(26);
                int voiceBoxW = 120;
                masterVoiceBox.setBounds(row.removeFromLeft(voiceBoxW).reduced(0, 1));
                row.removeFromLeft(16);

                bool isPoly = (masterVoiceBox.getSelectedId() <= 1);
                if (!isPoly)
                {
                    masterGlideSlider.setVisible(true);
                    int glideSliderW = 180;
                    masterGlideSlider.setBounds(row.removeFromLeft(glideSliderW));
                    row.removeFromLeft(16);
                }
                else
                {
                    masterGlideSlider.setVisible(false);
                }
                voiceModeDescLabel.setBounds(row);
            }
            else // Filters
            {
                lpfSectionLabel.setVisible(true);
                lpfEnableButton.setVisible(true);
                lpfCutoffSlider.setVisible(true);
                lpfResoSlider.setVisible(true);
                hpfSectionLabel.setVisible(true);
                hpfEnableButton.setVisible(true);
                hpfCutoffSlider.setVisible(true);
                hpfResoSlider.setVisible(true);

                int gap = 12;
                int halfW = (eCol.getWidth() - gap) / 2;
                auto lpfArea = eCol.removeFromLeft(halfW);
                eCol.removeFromLeft(gap);
                auto hpfArea = eCol;

                auto lHeader = lpfArea.removeFromTop(16);
                lpfEnableButton.setBounds(lHeader.removeFromRight(32).reduced(0, 1));
                lHeader.removeFromRight(4);
                lpfSectionLabel.setBounds(lHeader);
                lpfArea.removeFromTop(2);
                lpfCutoffSlider.setBounds(lpfArea.removeFromTop(22));
                lpfArea.removeFromTop(4);
                lpfResoSlider.setBounds(lpfArea.removeFromTop(22));

                auto hHeader = hpfArea.removeFromTop(16);
                hpfEnableButton.setBounds(hHeader.removeFromRight(32).reduced(0, 1));
                hHeader.removeFromRight(4);
                hpfSectionLabel.setBounds(hHeader);
                hpfArea.removeFromTop(2);
                hpfCutoffSlider.setBounds(hpfArea.removeFromTop(22));
                hpfArea.removeFromTop(4);
                hpfResoSlider.setBounds(hpfArea.removeFromTop(22));
            }
        }
    };

    if (!showSpectrograph)
    {
        // =====================================================================
        // GRAPH (SPECTROGRAPH) IS MINIMIZED:
        // The visualizer graph is hidden.
        // Partials GRID pops up into the vacated empty space!
        // =====================================================================
        if (showScope)
        {
            // Bottom row: Oscilloscope (left) + Master controls (right)
            int bottomRowHeight = 200;
            auto bottomRow = mainWorkspace.removeFromBottom(bottomRowHeight);
            mainWorkspace.removeFromBottom(8); // Spacing above bottom row

            oscilloscope.setBounds(bottomRow.removeFromLeft(bottomRowHeight).reduced(2));
            bottomRow.removeFromLeft(12);

            layoutMasterControls(bottomRow, true);
        }
        else
        {
            // Both graphs are minimized:
            // Master Settings horizontally at the bottom
            int masterHeight = 84;
            auto masterArea = mainWorkspace.removeFromBottom(masterHeight);
            mainWorkspace.removeFromBottom(8); // Spacing above master panel

            layoutMasterControls(masterArea, false);
        }

        // Partials Grid occupies the remaining workspace (pops up into the empty top space!)
        bool hasPartials = (numPartials > 0);
        partialsLabel.setVisible(hasPartials);
        partialsViewport.setVisible(hasPartials);

        if (hasPartials)
        {
            partialsLabel.setBounds(mainWorkspace.removeFromTop(20));
            mainWorkspace.removeFromTop(4);
            partialsViewport.setBounds(mainWorkspace);

            int vpW = juce::jmax(10, partialsViewport.getWidth() - 14);
            partialsContainer.updateLayout(vpW);
        }
    }
    else
    {
        // =====================================================================
        // GRAPH (SPECTROGRAPH) IS OPEN:
        // Right side: Harmonics pane (single column)
        // Left side: Spectrograph (top) + Bottom row (Scope / Master)
        // =====================================================================
        int harmonicsWidth = (numPartials > 0) ? 246 : 0;
        auto rightPane = mainWorkspace.removeFromRight(harmonicsWidth);

        bool hasPartials = (numPartials > 0);
        partialsLabel.setVisible(hasPartials);
        partialsViewport.setVisible(hasPartials);

        if (hasPartials)
        {
            partialsLabel.setBounds(rightPane.removeFromTop(20));
            rightPane.removeFromTop(4);
            partialsViewport.setBounds(rightPane);

            int vpW = juce::jmax(10, partialsViewport.getWidth() - 14);
            partialsContainer.updateLayout(vpW);
        }

        if (numPartials > 0)
            mainWorkspace.removeFromRight(8); // Spacing between left area and right pane

        // Left Area:
        if (showScope)
        {
            // Bottom row: Oscilloscope + Master controls
            int bottomRowHeight = 200;
            auto bottomRow = mainWorkspace.removeFromBottom(bottomRowHeight);

            oscilloscope.setBounds(bottomRow.removeFromLeft(bottomRowHeight).reduced(2));
            bottomRow.removeFromLeft(12);

            layoutMasterControls(bottomRow, true);
        }
        else
        {
            // Scope closed, Spectrograph open: Master controls horizontally across bottom of left area
            int masterHeight = 84;
            auto masterArea = mainWorkspace.removeFromBottom(masterHeight);

            layoutMasterControls(masterArea, false);
        }

        mainWorkspace.removeFromBottom(8);

        // Remaining top area: Spectrograph
        spectrograph.setBounds(mainWorkspace.reduced(2));
    }
}

void WaveSlaveAudioProcessorEditor::valueTreePropertyChanged(juce::ValueTree& treeWhosePropertyHasChanged, const juce::Identifier& property)
{
    juce::ignoreUnused(treeWhosePropertyHasChanged);

    if (property.toString() == "BASE_WAVEFORM")
    {
        int waveType = (int)audioProcessor.apvts.getRawParameterValue("BASE_WAVEFORM")->load();
        if (waveType >= 0 && waveType < 8)
        {
            audioProcessor.generateStandardWaveToCustomTable(waveType);
        }
    }

    if (property.toString() == "VOICE_MODE")
    {
        juce::MessageManager::callAsync([this]() {
            updateVoiceDesc();
        });
    }

    if (!isApplyingPreset && (factoryPresetBox.getSelectedId() > 0 || factoryPresetBox.getText() == "Random"))
    {
        if (hasAnyParameterDivergedFromSnapshot())
        {
            juce::MessageManager::callAsync([this]() {
                factoryPresetBox.setTextWhenNothingSelected("Custom");
                if (factoryPresetBox.getSelectedId() != 0)
                    factoryPresetBox.setSelectedId(0, juce::dontSendNotification);
            });
        }
    }
}

// ---------------------------------------------------------------------------
// Keyboard / MIDI Panic
// ---------------------------------------------------------------------------
bool WaveSlaveAudioProcessorEditor::keyPressed(const juce::KeyPress& key)
{
    // MIDI Panic: Escape key
    if (key.getKeyCode() == juce::KeyPress::escapeKey)
    {
        audioProcessor.allNotesOff();
        return true;
    }
    return false;
}

void WaveSlaveAudioProcessorEditor::updateVoiceDesc()
{
    int id = masterVoiceBox.getSelectedId();
    if (id <= 1)
        voiceModeDescLabel.setText("Poly: multiple notes simultaneously", juce::dontSendNotification);
    else if (id == 2)
        voiceModeDescLabel.setText("Mono: re-triggers envelope on each note", juce::dontSendNotification);
    else if (id == 3)
        voiceModeDescLabel.setText("Legato: glides pitch without re-triggering ADSR", juce::dontSendNotification);

    resized();
}


