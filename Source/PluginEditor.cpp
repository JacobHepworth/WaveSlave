#include "PluginProcessor.h"
#include "PluginEditor.h"

WaveSlaveAudioProcessorEditor::WaveSlaveAudioProcessorEditor (WaveSlaveAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p), oscilloscope(p), spectrograph(p)
{
    setLookAndFeel(&customLookAndFeel);
    juce::LookAndFeel::setDefaultLookAndFeel(&customLookAndFeel);

    // Visualizers
    addAndMakeVisible(oscilloscope);
    addAndMakeVisible(spectrograph);

    // Title
    titleLabel.setText("WAVESLAVE", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(20.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xFFFFFFFF));
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(titleLabel);

    // Presets & Prev/Next Auditioning
    auto getTargetPresetId = [this](bool next) -> int
    {
        std::vector<int> itemIds;
        for (int i = 0; i < factoryPresetBox.getNumItems(); ++i)
        {
            int id = factoryPresetBox.getItemId(i);
            if (id > 0)
                itemIds.push_back(id);
        }

        if (itemIds.empty())
            return 1;

        int activeId = factoryPresetBox.getSelectedId();
        if (activeId <= 0)
        {
            auto text = (factoryPresetBox.getSelectedId() > 0)
                            ? factoryPresetBox.getText()
                            : factoryPresetBox.getTextWhenNothingSelected();
            if (text == "Random")
                activeId = 17;
            else if (text == "Presets" || text == "Reset")
                activeId = 1;
            else if (currentPresetId > 0)
                activeId = currentPresetId;
            else
                activeId = 1;
        }

        int currentIdx = -1;
        for (int i = 0; i < (int)itemIds.size(); ++i)
        {
            if (itemIds[(size_t)i] == activeId)
            {
                currentIdx = i;
                break;
            }
        }

        if (currentIdx == -1)
            currentIdx = 0;

        int targetIdx = 0;
        if (next)
            targetIdx = (currentIdx + 1) % (int)itemIds.size();
        else
            targetIdx = (currentIdx - 1 + (int)itemIds.size()) % (int)itemIds.size();

        return itemIds[(size_t)targetIdx];
    };

    auto switchPresetWithPrompt = [this](int nextId)
    {
        juce::String text = (factoryPresetBox.getSelectedId() > 0)
                                ? factoryPresetBox.getText()
                                : factoryPresetBox.getTextWhenNothingSelected();
        bool isRandom = (text == "Random" || currentPresetId == 17);
        bool isCustom = (text == "Custom" || hasAnyParameterDivergedFromSnapshot());
        bool promptNeeded = (isRandom || isCustom);

        auto doSwitch = [this, nextId]()
        {
            int prevId = factoryPresetBox.getSelectedId();
            currentPresetId = nextId;
            factoryPresetBox.setSelectedId(nextId, juce::sendNotificationSync);
            if (prevId == nextId)
            {
                if (nextId >= 1 && nextId <= 17)
                    applyFactoryPreset(nextId);
            }
        };

        if (promptNeeded)
        {
            juce::Component::SafePointer<WaveSlaveAudioProcessorEditor> safeThis(this);
            juce::AlertWindow::showOkCancelBox(
                juce::AlertWindow::QuestionIcon,
                "Discard Changes?",
                "A custom or randomly generated patch is currently active. Switching presets will discard your current sound. Do you want to continue?",
                "Discard & Switch",
                "Cancel",
                this,
                juce::ModalCallbackFunction::create([safeThis, nextId](int result)
                {
                    if (result == 1 && safeThis != nullptr)
                    {
                        int prevId = safeThis->factoryPresetBox.getSelectedId();
                        safeThis->currentPresetId = nextId;
                        safeThis->factoryPresetBox.setSelectedId(nextId, juce::sendNotificationSync);
                        if (prevId == nextId)
                        {
                            if (nextId >= 1 && nextId <= 17)
                                safeThis->applyFactoryPreset(nextId);
                        }
                    }
                }));
        }
        else
        {
            doSwitch();
        }
    };

    prevPresetBtn.setTooltip("Previous Preset");
    prevPresetBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF252525));
    prevPresetBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    addAndMakeVisible(prevPresetBtn);
    prevPresetBtn.onClick = [this, getTargetPresetId, switchPresetWithPrompt]()
    {
        int nextId = getTargetPresetId(false);
        switchPresetWithPrompt(nextId);
    };

    nextPresetBtn.setTooltip("Next Preset");
    nextPresetBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF252525));
    nextPresetBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    addAndMakeVisible(nextPresetBtn);
    nextPresetBtn.onClick = [this, getTargetPresetId, switchPresetWithPrompt]()
    {
        int nextId = getTargetPresetId(true);
        switchPresetWithPrompt(nextId);
    };

    addAndMakeVisible(factoryPresetBox);
    factoryPresetBox.setScrollWheelEnabled(false);
    factoryPresetBox.setTextWhenNothingSelected("Presets");
    refreshPresetList();
    factoryPresetBox.setSelectedId(0, juce::dontSendNotification);
    factoryPresetBox.onChange = [this] 
    { 
        int id = factoryPresetBox.getSelectedId();
        if (id >= 1 && id <= 17)
        {
            currentPresetId = id;
            applyFactoryPreset(id);
        }
        else if (id >= 100)
        {
            currentPresetId = id;
            int userIdx = id - 100;
            if (userIdx >= 0 && userIdx < (int)userPresetFiles.size())
            {
                auto file = userPresetFiles[(size_t)userIdx];
                std::unique_ptr<juce::XmlElement> rootXml(juce::XmlDocument::parse(file));
                if (rootXml != nullptr)
                {
                    isApplyingPreset = true;
                    audioProcessor.allNotesOff();
                    resetAllPartials();

                    juce::XmlElement* paramXml = nullptr;
                    juce::XmlElement* wtXml = nullptr;
                    if (rootXml->hasTagName("WaveSlaveProfile") || rootXml->hasTagName("WaveSlavePreset"))
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
                        audioProcessor.apvts.replaceState(juce::ValueTree::fromXml(*paramXml));
                    audioProcessor.deserializeAnimationsFromXml(*rootXml);

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
                            }
                        }
                    }

                    partialsContainer.refreshAllStrips();
                    for (int i = 0; i < 16; ++i)
                        spectrograph.recordKeyframeFromSliders(i);
                    audioProcessor.syncLivePartialStateFromParams();
                    spectrograph.repaint();
                    oscilloscope.repaint();
                    recordPresetSnapshot();
                    juce::Component::SafePointer<WaveSlaveAudioProcessorEditor> safeThis(this);
                    juce::MessageManager::callAsync([safeThis]() {
                        if (safeThis != nullptr)
                        {
                            safeThis->recordPresetSnapshot();
                            safeThis->isApplyingPreset = false;
                        }
                    });
                }
            }
        }
    };
    
    addChildComponent(regenerateButton);
    regenerateButton.setTooltip("Regenerate a new random sound profile");
    regenerateButton.onClick = [this]()
    {
        juce::Component::SafePointer<WaveSlaveAudioProcessorEditor> safeThis(this);
        juce::AlertWindow::showOkCancelBox(
            juce::AlertWindow::QuestionIcon,
            "Regenerate Random Profile",
            "Are you sure you want to regenerate a new random patch? Current unsaved settings will be overwritten.",
            "Regenerate",
            "Cancel",
            this,
            juce::ModalCallbackFunction::create([safeThis](int result)
            {
                if (safeThis != nullptr && result == 1)
                {
                    safeThis->applyFactoryPreset(17);
                }
            })
        );
    };

    addAndMakeVisible(fileMenuBox);
    fileMenuBox.setScrollWheelEnabled(false);
    fileMenuBox.setTextWhenNothingSelected("File");
    fileMenuBox.addItem("Save Profile...", 1);
    fileMenuBox.addItem("Load Profile...", 2);
    fileMenuBox.addSeparator();
    fileMenuBox.addItem("Save Preset...", 3);
    fileMenuBox.addItem("Load Preset...", 4);
    fileMenuBox.addSeparator();
    fileMenuBox.addItem("Save User Preset...", 5);
    fileMenuBox.addItem("Open Presets Folder...", 6);
    fileMenuBox.setSelectedId(0, juce::dontSendNotification);
    fileMenuBox.onChange = [this]() {
        int id = fileMenuBox.getSelectedId();
        if (id == 1 || id == 3) saveProfile();
        else if (id == 2 || id == 4) loadProfile();
        else if (id == 5) saveUserPreset();
        else if (id == 6) openPresetsFolder();
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

    susRelEnvLabel.setText("SUSTAIN / RELEASE", juce::dontSendNotification);
    susRelEnvLabel.setFont(juce::Font(10.0f, juce::Font::bold));
    susRelEnvLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.6f));
    susRelEnvLabel.setJustificationType(juce::Justification::centred);
    addChildComponent(susRelEnvLabel);

    // Partials section label
    partialsLabel.setText("PARTIALS", juce::dontSendNotification);
    partialsLabel.setFont(juce::Font(11.0f, juce::Font::bold));
    partialsLabel.setColour(juce::Label::textColourId, juce::Colour(0xFFFFFFFF));
    partialsLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(partialsLabel);

    // Global Master Gain & Pan sliders & Peak Meter
    addAndMakeVisible(masterGainSlider);
    masterGainSlider.setTextBoxWidth(46);
    masterGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "MASTER_GAIN", masterGainSlider.slider);

    addAndMakeVisible(masterPanSlider);
    masterPanSlider.setTextBoxWidth(46);
    masterPanSlider.slider.setDoubleClickReturnValue(true, 0.0);
    masterPanAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "MASTER_PAN", masterPanSlider.slider);

    addAndMakeVisible(peakMeter);



    addAndMakeVisible(partialsComboBox);
    partialsComboBox.setScrollWheelEnabled(false);
    for (int i = 1; i <= 16; ++i)
    {
        partialsComboBox.addItem(juce::String(i) + (i == 1 ? " Partial" : " Partials"), i);
    }

    // Combined Edit & Bulk Transformations dropdown
    addAndMakeVisible(editMenuBox);
    editMenuBox.setScrollWheelEnabled(false);
    editMenuBox.setTextWhenNothingSelected("Edit");
    editMenuBox.numColumns = 4;

    editMenuBox.addSectionHeading("Actions");
    editMenuBox.addItem("Reset All Partials",       99);
    editMenuBox.addItem("Randomize All Partials",   98);
    editMenuBox.addItem("Copy All Partials",         97);
    editMenuBox.addItem("Paste All Partials",        96);

    editMenuBox.addSectionHeading("Partials On/Off");
    editMenuBox.addItem("Enable All",                200);
    editMenuBox.addItem("Fundamental Only",          201);
    editMenuBox.addItem("Odd Partials Only",         202);
    editMenuBox.addItem("Even Partials Only",        203);

    editMenuBox.addSectionHeading("Waveform");
    editMenuBox.addItem("All Sine",          1);
    editMenuBox.addItem("All Triangle",      2);
    editMenuBox.addItem("All Sawtooth",      3);
    editMenuBox.addItem("All Square",        4);
    editMenuBox.addItem("All Pulse",         5);
    editMenuBox.addItem("All Sharkfin",      6);
    editMenuBox.addItem("All Fibonacci",     7);
    editMenuBox.addItem("All Custom 1",      8);
    editMenuBox.addItem("All Custom 2",      9);
    editMenuBox.addItem("All Custom 3",      100);
    editMenuBox.addItem("All Custom 4",      101);

    editMenuBox.addSectionHeading("Harmonic Ratios");
    editMenuBox.addItem("Natural Series",    10);
    editMenuBox.addItem("Odd Harmonics",     11);
    editMenuBox.addItem("Even Harmonics",    12);
    editMenuBox.addItem("Octaves",           13);
    editMenuBox.addItem("Inharmonic / Bell", 14);

    editMenuBox.addSectionHeading("Gain");
    editMenuBox.addItem("1/n Rolloff",            20);
    editMenuBox.addItem("1/n^2 Rolloff",          21);
    editMenuBox.addItem("Flat (Equal)",           22);
    editMenuBox.addItem("Inverted",               23);
    editMenuBox.addItem("Fundamental Only",       24);

    editMenuBox.addSectionHeading("Pan");
    editMenuBox.addItem("Center All",           35);
    editMenuBox.addItem("Alternate L/R",        36);
    editMenuBox.addItem("Wide L/R",             37);

    editMenuBox.addSectionHeading("Semitones");
    editMenuBox.addItem("+12 (Octave Up)",   41);
    editMenuBox.addItem("-12 (Octave Down)", 42);
    editMenuBox.addItem("+1 (Step Up)",      43);
    editMenuBox.addItem("-1 (Step Down)",    44);
    editMenuBox.addItem("Reset All (0 st)",  40);

    editMenuBox.addSectionHeading("Detune");
    editMenuBox.addItem("Reset All (0 ct)",       210);
    editMenuBox.addItem("Alternate +/-10 ct",     211);
    editMenuBox.addItem("Spread Up (+3 ct/step)", 212);
    editMenuBox.addItem("Spread Down (-3 ct/step)", 213);

    editMenuBox.addSectionHeading("Glide");
    editMenuBox.addItem("Off (0 s)",              260);
    editMenuBox.addItem("Short (0.1 s)",          261);
    editMenuBox.addItem("Long (0.5 s)",           262);
    editMenuBox.addItem("Staggered (rising)",     263);

    editMenuBox.addSectionHeading("LFO Target / Depth");
    editMenuBox.addItem("Target: None",          50);
    editMenuBox.addItem("Target: Pitch",         51);
    editMenuBox.addItem("Target: Level",         52);
    editMenuBox.addItem("Target: Pan",           54);
    editMenuBox.addItem("Depth: 0%",             240);
    editMenuBox.addItem("Depth: 25%",            241);
    editMenuBox.addItem("Depth: 50%",            53);
    editMenuBox.addItem("Depth: 100%",           242);

    editMenuBox.addSectionHeading("LFO Waveform");
    editMenuBox.addItem("All Sine",        110);
    editMenuBox.addItem("All Triangle",    111);
    editMenuBox.addItem("All Saw",         112);
    editMenuBox.addItem("All Square",      113);
    editMenuBox.addItem("All Custom 1",    114);
    editMenuBox.addItem("All Custom 2",    115);
    editMenuBox.addItem("All Custom 3",    116);
    editMenuBox.addItem("All Custom 4",    117);

    editMenuBox.addSectionHeading("LFO Rate");
    editMenuBox.addItem("0.5 Hz",               220);
    editMenuBox.addItem("2 Hz (Default)",       221);
    editMenuBox.addItem("5 Hz",                 222);
    editMenuBox.addItem("10 Hz",                223);
    editMenuBox.addItem("Staggered (0.5 Hz/step)", 224);

    editMenuBox.addSectionHeading("LFO Phase");
    editMenuBox.addItem("Reset All (0 deg)",    230);
    editMenuBox.addItem("Staggered",            231);
    editMenuBox.addItem("Alternate 0/180 deg",  232);

    editMenuBox.addSectionHeading("Per-Partial ADSR");
    editMenuBox.addItem("Enable All",           250);
    editMenuBox.addItem("Disable All",          251);
    editMenuBox.addItem("Preset: Pluck",        252);
    editMenuBox.addItem("Preset: Pad",          253);
    editMenuBox.addItem("Preset: Organ",        254);
    editMenuBox.addItem("Stagger Attack",       255);
    editMenuBox.addItem("Stagger Release",      256);

    editMenuBox.addSectionHeading("Animation Mode");
    editMenuBox.addItem("Off",                  270);
    editMenuBox.addItem("Note Length",          271);
    editMenuBox.addItem("Time: One-Shot",       272);
    editMenuBox.addItem("Time: Loop",           273);
    editMenuBox.addItem("Time: Ping-Pong",      274);

    editMenuBox.addSectionHeading("Animation Curve");
    editMenuBox.addItem("Smooth",               280);
    editMenuBox.addItem("Linear",               281);
    editMenuBox.addItem("Step",                 282);

    editMenuBox.addSectionHeading("Animation Duration");
    editMenuBox.addItem("1 s",                  290);
    editMenuBox.addItem("2 s",                  291);
    editMenuBox.addItem("4 s",                  292);
    editMenuBox.addItem("8 s",                  293);

    editMenuBox.addSectionHeading("Animation Depth");
    editMenuBox.addItem("0%",                   300);
    editMenuBox.addItem("50%",                  301);
    editMenuBox.addItem("100%",                 302);

    editMenuBox.onChange = [this]()
    {
        int result = editMenuBox.getSelectedId();
        if (result == 0) return;
        editMenuBox.setSelectedId(0, juce::dontSendNotification);

        const auto isEven = [](int i) { return (i % 2) == 0; };

        if      (result == 99) applyFactoryPreset(1);
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
        else if (result == 35) applyBulkPan(0);
        else if (result == 36) applyBulkPan(1);
        else if (result == 37) applyBulkPan(2);
        else if (result == 40) applyBulkSemitones(0);
        else if (result == 41) applyBulkSemitoneShift(12);
        else if (result == 42) applyBulkSemitoneShift(-12);
        else if (result == 43) applyBulkSemitoneShift(1);
        else if (result == 44) applyBulkSemitoneShift(-1);
        else if (result == 50) applyBulkModTarget(0);
        else if (result == 51) applyBulkModTarget(1);
        else if (result == 52) applyBulkModTarget(2);
        else if (result == 54) applyBulkModTarget(3);
        else if (result == 53) applyBulkModDepth(0.5f);
        else if (result == 110) applyBulkModWaveform(0);
        else if (result == 111) applyBulkModWaveform(1);
        else if (result == 112) applyBulkModWaveform(2);
        else if (result == 113) applyBulkModWaveform(3);
        else if (result == 114) applyBulkModWaveform(4); // Custom 1
        else if (result == 115) applyBulkModWaveform(5); // Custom 2
        else if (result == 116) applyBulkModWaveform(6); // Custom 3
        else if (result == 117) applyBulkModWaveform(7); // Custom 4

        // Partial enable
        else if (result == 200) applyBulkParam("P_ENABLED", [](int)   { return 1.0f; });
        else if (result == 201) applyBulkParam("P_ENABLED", [](int i) { return i == 1 ? 1.0f : 0.0f; });
        else if (result == 202) applyBulkParam("P_ENABLED", [&](int i) { return isEven(i) ? 0.0f : 1.0f; });
        else if (result == 203) applyBulkParam("P_ENABLED", [&](int i) { return isEven(i) ? 1.0f : 0.0f; });

        // Detune (cents)
        else if (result == 210) { applyBulkParam("DETUNE", [](int)   { return 0.0f; }); for (int i = 0; i < 16; ++i) spectrograph.recordKeyframeFromSliders(i); }
        else if (result == 211) { applyBulkParam("DETUNE", [&](int i) { return isEven(i) ? 10.0f : -10.0f; }); for (int i = 0; i < 16; ++i) spectrograph.recordKeyframeFromSliders(i); }
        else if (result == 212) { applyBulkParam("DETUNE", [](int i) { return 3.0f * (float)(i - 1); }); for (int i = 0; i < 16; ++i) spectrograph.recordKeyframeFromSliders(i); }
        else if (result == 213) { applyBulkParam("DETUNE", [](int i) { return -3.0f * (float)(i - 1); }); for (int i = 0; i < 16; ++i) spectrograph.recordKeyframeFromSliders(i); }

        // LFO rate (Hz)
        else if (result == 220) applyBulkParam("MOD_RATE", [](int) { return 0.5f; });
        else if (result == 221) applyBulkParam("MOD_RATE", [](int) { return 2.0f; });
        else if (result == 222) applyBulkParam("MOD_RATE", [](int) { return 5.0f; });
        else if (result == 223) applyBulkParam("MOD_RATE", [](int) { return 10.0f; });
        else if (result == 224) applyBulkParam("MOD_RATE", [](int i) { return 0.5f * (float)i; });

        // LFO phase (degrees)
        else if (result == 230) applyBulkParam("MOD_PHASE", [](int)   { return 0.0f; });
        else if (result == 231) applyBulkParam("MOD_PHASE", [](int i) { return 360.0f * (float)(i - 1) / 16.0f; });
        else if (result == 232) applyBulkParam("MOD_PHASE", [&](int i) { return isEven(i) ? 180.0f : 0.0f; });

        // LFO depth
        else if (result == 240) applyBulkModDepth(0.0f);
        else if (result == 241) applyBulkModDepth(0.25f);
        else if (result == 242) applyBulkModDepth(1.0f);

        // Per-partial ADSR
        else if (result >= 250 && result <= 256) applyBulkPartialAdsr(result - 250);

        // Glide (seconds)
        else if (result == 260) applyBulkParam("P_GLIDE", [](int) { return 0.0f; });
        else if (result == 261) applyBulkParam("P_GLIDE", [](int) { return 0.1f; });
        else if (result == 262) applyBulkParam("P_GLIDE", [](int) { return 0.5f; });
        else if (result == 263) applyBulkParam("P_GLIDE", [](int i) { return 0.05f * (float)(i - 1); });

        // Animation
        else if (result >= 270 && result <= 274)
        {
            applyBulkParam("ANIM_MODE", [result](int) { return (float)(result - 270); });
            if (result > 270)
            {
                for (int i = 0; i < 16; ++i)
                    audioProcessor.ensureInitialKeyframes(i);
            }
            spectrograph.syncTimelineControlsToSelectedPartial();
        }
        else if (result >= 280 && result <= 282) applyBulkParam("ANIM_CURVE", [result](int) { return (float)(result - 280); });
        else if (result >= 290 && result <= 293) applyBulkParam("ANIM_DURATION", [result](int) { return (float)(1 << (result - 290)); });
        else if (result == 300) applyBulkParam("ANIM_DEPTH", [](int) { return 0.0f; });
        else if (result == 301) applyBulkParam("ANIM_DEPTH", [](int) { return 0.5f; });
        else if (result == 302) applyBulkParam("ANIM_DEPTH", [](int) { return 1.0f; });

        audioProcessor.syncLivePartialStateFromParams();
        partialsContainer.refreshAllStrips();
        spectrograph.repaint();
        oscilloscope.repaint();
        editMenuBox.setSelectedId(0, juce::dontSendNotification);
    };

    // ADSR
    addAndMakeVisible(attackVSlider);
    attackVSlider.setTextBoxWidth(46);
    attackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "ATTACK", attackVSlider.slider);

    addAndMakeVisible(decayVSlider);
    decayVSlider.setTextBoxWidth(46);
    decayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "DECAY", decayVSlider.slider);

    addAndMakeVisible(sustainVSlider);
    sustainVSlider.setTextBoxWidth(46);
    sustainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "SUSTAIN", sustainVSlider.slider);

    addAndMakeVisible(releaseVSlider);
    releaseVSlider.setTextBoxWidth(46);
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

    auto freqTextFromValue = [](double val) {
        if (val >= 1000.0)
            return juce::String(juce::roundToInt(val)) + " Hz";
        if (val >= 100.0)
            return juce::String(val, 0) + " Hz";
        return juce::String(val, 1) + " Hz";
    };
    auto freqValueFromText = [](const juce::String& text) -> double {
        auto t = text.trim();
        if (t.endsWithIgnoreCase("kHz"))
            return t.dropLastCharacters(3).trim().getDoubleValue() * 1000.0;
        if (t.endsWithIgnoreCase("k"))
            return t.dropLastCharacters(1).trim().getDoubleValue() * 1000.0;
        if (t.endsWithIgnoreCase("Hz"))
            return t.dropLastCharacters(2).trim().getDoubleValue();
        return t.getDoubleValue();
    };

    addChildComponent(lpfCutoffSlider);
    lpfCutoffSlider.setTextBoxWidth(68);
    lpfCutoffSlider.slider.textFromValueFunction = freqTextFromValue;
    lpfCutoffSlider.slider.valueFromTextFunction = freqValueFromText;
    lpfCutoffAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "LPF_CUTOFF", lpfCutoffSlider.slider);

    addChildComponent(lpfResoSlider);
    lpfResoSlider.setTextBoxWidth(48);
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
    hpfCutoffSlider.setTextBoxWidth(68);
    hpfCutoffSlider.slider.textFromValueFunction = freqTextFromValue;
    hpfCutoffSlider.slider.valueFromTextFunction = freqValueFromText;
    hpfCutoffAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "HPF_CUTOFF", hpfCutoffSlider.slider);

    addChildComponent(hpfResoSlider);
    hpfResoSlider.setTextBoxWidth(48);
    hpfResoAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "HPF_RESO", hpfResoSlider.slider);

    addChildComponent(lpfEnvSlider);
    lpfEnvSlider.setTextBoxWidth(48);
    lpfEnvAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "LPF_ENV_AMT", lpfEnvSlider.slider);

    addChildComponent(hpfEnvSlider);
    hpfEnvSlider.setTextBoxWidth(48);
    hpfEnvAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "HPF_ENV_AMT", hpfEnvSlider.slider);

    lpfEnableButton.onStateChange = [this]() { updateFilterButtonsText(); };
    hpfEnableButton.onStateChange = [this]() { updateFilterButtonsText(); };
    updateFilterButtonsText();

    // Master Voice Settings UI
    addChildComponent(voiceSectionLabel);
    voiceSectionLabel.setText("VOICE & PITCH", juce::dontSendNotification);
    voiceSectionLabel.setFont(juce::Font(10.0f, juce::Font::bold));
    voiceSectionLabel.setJustificationType(juce::Justification::centredLeft);
    voiceSectionLabel.setColour(juce::Label::textColourId, juce::Colour(0xFFFFFFFF));

    addChildComponent(voiceModeLabel);
    voiceModeLabel.setText("MODE", juce::dontSendNotification);
    voiceModeLabel.setFont(juce::Font(10.0f, juce::Font::bold));
    voiceModeLabel.setJustificationType(juce::Justification::centredLeft);
    voiceModeLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.7f));

    addChildComponent(masterVoiceBox);
    masterVoiceBox.setScrollWheelEnabled(false);
    masterVoiceBox.addItemList({ "Poly", "Mono", "Legato" }, 1);
    masterVoiceAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.apvts, "VOICE_MODE", masterVoiceBox);

    addChildComponent(masterGlideSlider);
    masterGlideSlider.slider.setTextValueSuffix(" s");
    masterGlideSlider.setTextBoxWidth(52, 18);
    masterGlideSlider.slider.setNumDecimalPlacesToDisplay(2);
    masterGlideAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "GLIDE_TIME", masterGlideSlider.slider);

    addChildComponent(phaseModeLabel);
    phaseModeLabel.setText("PHASE", juce::dontSendNotification);
    phaseModeLabel.setFont(juce::Font(10.0f, juce::Font::bold));
    phaseModeLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.7f));
    addChildComponent(phaseModeBox);
    phaseModeBox.addItemList({ "Key-Sync (0 deg)", "Free-Running", "Random Phase" }, 1);
    phaseModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.apvts, "PHASE_MODE", phaseModeBox);

    addChildComponent(pitchBendLabel);
    pitchBendLabel.setText("BEND RANGE", juce::dontSendNotification);
    pitchBendLabel.setFont(juce::Font(10.0f, juce::Font::bold));
    pitchBendLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.7f));
    addChildComponent(pitchBendBox);
    pitchBendBox.addItemList({ "+/- 2 st", "+/- 7 st", "+/- 12 st", "+/- 24 st" }, 1);
    pitchBendAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.apvts, "PITCH_BEND_RANGE", pitchBendBox);

    addChildComponent(masterClipperToggle);
    masterClipperToggle.setColour(juce::ToggleButton::textColourId, juce::Colours::white);
    masterClipperToggle.setColour(juce::ToggleButton::tickColourId, juce::Colours::white);
    masterClipperAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.apvts, "MASTER_CLIPPER", masterClipperToggle);

    addChildComponent(voiceModeDescLabel);
    voiceModeDescLabel.setFont(juce::Font(10.0f));
    voiceModeDescLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.6f));
    voiceModeDescLabel.setJustificationType(juce::Justification::centredLeft);

    masterVoiceBox.onChange = [this]() { updateVoiceDesc(); };
    updateVoiceDesc();

    // Master FX Settings UI (Chorus, Ping-Pong Delay, Reverb)
    addChildComponent(chorusLabel);
    chorusLabel.setText("CHORUS", juce::dontSendNotification);
    chorusLabel.setFont(juce::Font(10.0f, juce::Font::bold));
    chorusLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addChildComponent(chorusEnableBtn);
    chorusEnableBtn.setButtonText("OFF");
    chorusEnableBtn.setClickingTogglesState(true);
    chorusEnableBtn.setTooltip("Enable / Disable Stereo Chorus");
    chorusEnableAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.apvts, "CHORUS_ENABLED", chorusEnableBtn);
    chorusEnableBtn.onStateChange = [this]() { updateFxButtonsText(); };

    addChildComponent(chorusRateSlider);
    chorusRateSlider.slider.setTooltip("Chorus LFO Rate (0.1 - 10.0 Hz)");
    chorusRateSlider.setLabelPosition(LabeledSlider::LabelPosition::Top);
    chorusRateSlider.setTextBoxWidth(38, 14);
    chorusRateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "CHORUS_RATE", chorusRateSlider.slider);

    addChildComponent(chorusDepthSlider);
    chorusDepthSlider.slider.setTooltip("Chorus Modulation Depth (0 - 100%)");
    chorusDepthSlider.setLabelPosition(LabeledSlider::LabelPosition::Top);
    chorusDepthSlider.setTextBoxWidth(38, 14);
    chorusDepthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "CHORUS_DEPTH", chorusDepthSlider.slider);

    addChildComponent(chorusMixSlider);
    chorusMixSlider.slider.setTooltip("Chorus Wet / Dry Mix (0 - 100%)");
    chorusMixSlider.setLabelPosition(LabeledSlider::LabelPosition::Top);
    chorusMixSlider.setTextBoxWidth(38, 14);
    chorusMixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "CHORUS_MIX", chorusMixSlider.slider);

    addChildComponent(delayLabel);
    delayLabel.setText("DELAY", juce::dontSendNotification);
    delayLabel.setFont(juce::Font(10.0f, juce::Font::bold));
    delayLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addChildComponent(delayEnableBtn);
    delayEnableBtn.setButtonText("OFF");
    delayEnableBtn.setClickingTogglesState(true);
    delayEnableBtn.setTooltip("Enable / Disable Ping-Pong Delay");
    delayEnableAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.apvts, "DELAY_ENABLED", delayEnableBtn);
    delayEnableBtn.onStateChange = [this]() { updateFxButtonsText(); };

    addChildComponent(delayTimeSlider);
    delayTimeSlider.slider.setTooltip("Delay Time in seconds");
    delayTimeSlider.setLabelPosition(LabeledSlider::LabelPosition::Top);
    delayTimeSlider.setTextBoxWidth(38, 14);
    delayTimeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "DELAY_TIME", delayTimeSlider.slider);

    addChildComponent(delayFeedbackSlider);
    delayFeedbackSlider.slider.setTooltip("Delay Stereo Feedback (0 - 95%)");
    delayFeedbackSlider.setLabelPosition(LabeledSlider::LabelPosition::Top);
    delayFeedbackSlider.setTextBoxWidth(38, 14);
    delayFeedbackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "DELAY_FEEDBACK", delayFeedbackSlider.slider);

    addChildComponent(delayMixSlider);
    delayMixSlider.slider.setTooltip("Delay Wet / Dry Mix (0 - 100%)");
    delayMixSlider.setLabelPosition(LabeledSlider::LabelPosition::Top);
    delayMixSlider.setTextBoxWidth(38, 14);
    delayMixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "DELAY_MIX", delayMixSlider.slider);

    addChildComponent(reverbLabel);
    reverbLabel.setText("REVERB", juce::dontSendNotification);
    reverbLabel.setFont(juce::Font(10.0f, juce::Font::bold));
    reverbLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addChildComponent(reverbEnableBtn);
    reverbEnableBtn.setButtonText("OFF");
    reverbEnableBtn.setClickingTogglesState(true);
    reverbEnableBtn.setTooltip("Enable / Disable Stereo Reverb");
    reverbEnableAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.apvts, "REVERB_ENABLED", reverbEnableBtn);
    reverbEnableBtn.onStateChange = [this]() { updateFxButtonsText(); };

    addChildComponent(reverbSizeSlider);
    reverbSizeSlider.slider.setTooltip("Reverb Room Size (0 - 100%)");
    reverbSizeSlider.setLabelPosition(LabeledSlider::LabelPosition::Top);
    reverbSizeSlider.setTextBoxWidth(38, 14);
    reverbSizeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "REVERB_SIZE", reverbSizeSlider.slider);

    addChildComponent(reverbDampSlider);
    reverbDampSlider.slider.setTooltip("Reverb High Frequency Damping (0 - 100%)");
    reverbDampSlider.setLabelPosition(LabeledSlider::LabelPosition::Top);
    reverbDampSlider.setTextBoxWidth(38, 14);
    reverbDampAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "REVERB_DAMP", reverbDampSlider.slider);

    addChildComponent(reverbMixSlider);
    reverbMixSlider.slider.setTooltip("Reverb Wet / Dry Mix (0 - 100%)");
    reverbMixSlider.setLabelPosition(LabeledSlider::LabelPosition::Top);
    reverbMixSlider.setTextBoxWidth(38, 14);
    reverbMixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "REVERB_MIX", reverbMixSlider.slider);

    updateFxButtonsText();

    // Partials viewport (vertical scroll on right side)
    addAndMakeVisible(partialsViewport);
    partialsViewport.setViewedComponent(&partialsContainer, false);
    partialsViewport.setScrollBarsShown(true, false); // vertical scroll only
    partialsViewport.addMouseListener(this, false);

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

    partialsContainer.initPartials(audioProcessor.apvts, audioProcessor.partialSoloState.data(), 16, [this](int idx)
    {
        partialsContainer.setSelectedPartial(idx);
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
    partialsLabel.setVisible(false);
    partialsViewport.setVisible(true);

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
    setSize (audioProcessor.editorWidth > 0 ? audioProcessor.editorWidth : 940,
             audioProcessor.editorHeight > 0 ? audioProcessor.editorHeight : 660);
}

WaveSlaveAudioProcessorEditor::~WaveSlaveAudioProcessorEditor()
{
    audioProcessor.apvts.removeParameterListener("NUM_PARTIALS", this);
    audioProcessor.apvts.state.removeListener(this);
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
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
        float bottomH = showScope ? 200.0f : 138.0f;
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

    // Level gauge acts as natural separator under Master & Pan; vertical dividers separate columns
    if (outputLabel.isVisible() && peakMeter.isVisible())
    {
        float leftX = (float)outputLabel.getX();
        float rightX = (float)peakMeter.getRight();
        float sepY = (float)peakMeter.getBottom() + 3.0f;
        if (rightX > leftX && sepY > 0)
        {
            // Vertical column dividers for active panel underneath
            if (chorusLabel.isVisible() && delayLabel.isVisible() && reverbLabel.isVisible())
            {
                float col1SepX = (float)delayLabel.getX() - 6.0f;
                float col2SepX = (float)reverbLabel.getX() - 6.0f;
                float colTopY = sepY + 1.0f;
                float colBotY = (float)chorusMixSlider.getBottom();
                if (colBotY > colTopY)
                {
                    g.setColour(juce::Colour(0xFF333333));
                    g.drawLine(col1SepX, colTopY, col1SepX, colBotY, 1.0f);
                    g.drawLine(col2SepX, colTopY, col2SepX, colBotY, 1.0f);
                }
            }
            else if (lpfSectionLabel.isVisible() && hpfSectionLabel.isVisible())
            {
                float colSepX = (float)hpfSectionLabel.getX() - 8.0f;
                float colTopY = sepY + 1.0f;
                float colBotY = (float)lpfEnvSlider.getBottom();
                if (colBotY > colTopY)
                {
                    g.setColour(juce::Colour(0xFF333333));
                    g.drawLine(colSepX, colTopY, colSepX, colBotY, 1.0f);
                }
            }
            else if (mainEnvLabel.isVisible() && sustainVSlider.isVisible())
            {
                float colSepX = (float)sustainVSlider.getX() - 10.0f;
                float colTopY = sepY + 1.0f;
                float colBotY = (float)juce::jmax(decayVSlider.getBottom(), releaseVSlider.getBottom());
                if (colBotY > colTopY && colSepX > leftX)
                {
                    g.setColour(juce::Colour(0xFF333333));
                    g.drawLine(colSepX, colTopY, colSepX, colBotY, 1.0f);
                }
            }
            else if (voiceSectionLabel.isVisible() && phaseModeBox.isVisible())
            {
                float colSepX = (float)phaseModeBox.getX() - 8.0f;
                float colTopY = sepY + 1.0f;
                float colBotY = (float)pitchBendBox.getBottom();
                if (colBotY > colTopY && colSepX > leftX)
                {
                    g.setColour(juce::Colour(0xFF333333));
                    g.drawLine(colSepX, colTopY, colSepX, colBotY, 1.0f);
                }
            }
        }
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
    
    partialsLabel.setVisible(false);
    partialsViewport.setVisible(true);

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
        spectrograph.recordKeyframeFromSliders(i - 1);
    }
    spectrograph.repaint();
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
        spectrograph.recordKeyframeFromSliders(i - 1);
    }
    spectrograph.repaint();
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
        spectrograph.recordKeyframeFromSliders(i - 1);
    }
    spectrograph.repaint();
}

void WaveSlaveAudioProcessorEditor::applyBulkSemitoneShift(int delta)
{
    for (int i = 1; i <= 16; ++i)
    {
        if (auto* p = audioProcessor.apvts.getParameter("SEMITONE" + juce::String(i)))
        {
            float curSemi = p->convertFrom0to1(p->getValue());
            float newSemi = juce::jlimit(-24.0f, 24.0f, curSemi + (float)delta);
            p->beginChangeGesture();
            p->setValueNotifyingHost(p->convertTo0to1(newSemi));
            p->endChangeGesture();
        }
        spectrograph.recordKeyframeFromSliders(i - 1);
    }
    spectrograph.repaint();
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
        spectrograph.recordKeyframeFromSliders(i - 1);
    }
    spectrograph.repaint();
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

void WaveSlaveAudioProcessorEditor::applyBulkParam(const juce::String& paramPrefix, const std::function<float(int)>& fn)
{
    for (int i = 1; i <= 16; ++i)
    {
        if (auto* p = audioProcessor.apvts.getParameter(paramPrefix + juce::String(i)))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost(p->convertTo0to1(fn(i)));
            p->endChangeGesture();
        }
    }
}

void WaveSlaveAudioProcessorEditor::applyBulkPartialAdsr(int preset)
{
    if (preset == 0 || preset == 1)
    {
        applyBulkParam("P_ADSR_ENABLED", [preset](int) { return preset == 0 ? 1.0f : 0.0f; });
        return;
    }

    // Presets and staggers always turn the per-partial ADSR on
    applyBulkParam("P_ADSR_ENABLED", [](int) { return 1.0f; });

    switch (preset)
    {
        case 2: // Pluck
            applyBulkParam("P_ATTACK",  [](int)   { return 0.002f; });
            applyBulkParam("P_DECAY",   [](int i) { return 0.35f / (float)i + 0.05f; });
            applyBulkParam("P_SUSTAIN", [](int)   { return 0.0f; });
            applyBulkParam("P_RELEASE", [](int)   { return 0.2f; });
            break;
        case 3: // Pad
            applyBulkParam("P_ATTACK",  [](int)   { return 1.0f; });
            applyBulkParam("P_DECAY",   [](int)   { return 1.0f; });
            applyBulkParam("P_SUSTAIN", [](int)   { return 0.8f; });
            applyBulkParam("P_RELEASE", [](int)   { return 2.0f; });
            break;
        case 4: // Organ
            applyBulkParam("P_ATTACK",  [](int)   { return 0.005f; });
            applyBulkParam("P_DECAY",   [](int)   { return 0.05f; });
            applyBulkParam("P_SUSTAIN", [](int)   { return 1.0f; });
            applyBulkParam("P_RELEASE", [](int)   { return 0.05f; });
            break;
        case 5: // Stagger attack: higher partials fade in later
            applyBulkParam("P_ATTACK",  [](int i) { return 0.01f + 0.1f * (float)(i - 1); });
            break;
        case 6: // Stagger release: higher partials die away sooner
            applyBulkParam("P_RELEASE", [](int i) { return juce::jmax(0.05f, 2.0f - 0.12f * (float)(i - 1)); });
            break;
        default: break;
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

    if (auto* p = audioProcessor.apvts.getParameter("CHORUS_ENABLED")) {
        p->beginChangeGesture(); p->setValueNotifyingHost(0.0f); p->endChangeGesture();
    }
    if (auto* p = audioProcessor.apvts.getParameter("DELAY_ENABLED")) {
        p->beginChangeGesture(); p->setValueNotifyingHost(0.0f); p->endChangeGesture();
    }
    if (auto* p = audioProcessor.apvts.getParameter("REVERB_ENABLED")) {
        p->beginChangeGesture(); p->setValueNotifyingHost(0.0f); p->endChangeGesture();
    }
    updateFxButtonsText();
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

    for (int i = 0; i < 16; ++i)
        spectrograph.recordKeyframeFromSliders(i);
    audioProcessor.syncLivePartialStateFromParams();
    spectrograph.syncTimelineControlsToSelectedPartial();
    partialsContainer.refreshAllStrips();
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
        if (auto* p = audioProcessor.apvts.getParameter("SEMITONE" + s))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost(p->convertTo0to1(0.0f));
            p->endChangeGesture();
        }
        if (auto* p = audioProcessor.apvts.getParameter("DETUNE" + s))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost(p->convertTo0to1(0.0f));
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
        spectrograph.recordKeyframeFromSliders(i - 1);
    }
    audioProcessor.syncLivePartialStateFromParams();
    partialsContainer.refreshAllStrips();
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

void WaveSlaveAudioProcessorEditor::updateFilterButtonsText()
{
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
}

void WaveSlaveAudioProcessorEditor::updateFxButtonsText()
{
    // Chorus
    bool cOn = chorusEnableBtn.getToggleState();
    chorusEnableBtn.setButtonText(cOn ? "ON" : "OFF");
    chorusEnableBtn.setColour(juce::TextButton::buttonColourId, cOn ? juce::Colour(0xFF383838) : juce::Colour(0xFF222222));
    chorusEnableBtn.setColour(juce::TextButton::buttonOnColourId, cOn ? juce::Colour(0xFF383838) : juce::Colour(0xFF222222));
    chorusEnableBtn.setColour(juce::TextButton::textColourOnId, juce::Colour(0xFFFFFFFF));
    chorusEnableBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xFF777777));
    chorusRateSlider.setEnabled(cOn);
    chorusRateSlider.setAlpha(cOn ? 1.0f : 0.35f);
    chorusDepthSlider.setEnabled(cOn);
    chorusDepthSlider.setAlpha(cOn ? 1.0f : 0.35f);
    chorusMixSlider.setEnabled(cOn);
    chorusMixSlider.setAlpha(cOn ? 1.0f : 0.35f);

    // Ping-Pong Delay
    bool dOn = delayEnableBtn.getToggleState();
    delayEnableBtn.setButtonText(dOn ? "ON" : "OFF");
    delayEnableBtn.setColour(juce::TextButton::buttonColourId, dOn ? juce::Colour(0xFF383838) : juce::Colour(0xFF222222));
    delayEnableBtn.setColour(juce::TextButton::buttonOnColourId, dOn ? juce::Colour(0xFF383838) : juce::Colour(0xFF222222));
    delayEnableBtn.setColour(juce::TextButton::textColourOnId, juce::Colour(0xFFFFFFFF));
    delayEnableBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xFF777777));
    delayTimeSlider.setEnabled(dOn);
    delayTimeSlider.setAlpha(dOn ? 1.0f : 0.35f);
    delayFeedbackSlider.setEnabled(dOn);
    delayFeedbackSlider.setAlpha(dOn ? 1.0f : 0.35f);
    delayMixSlider.setEnabled(dOn);
    delayMixSlider.setAlpha(dOn ? 1.0f : 0.35f);

    // Reverb
    bool rOn = reverbEnableBtn.getToggleState();
    reverbEnableBtn.setButtonText(rOn ? "ON" : "OFF");
    reverbEnableBtn.setColour(juce::TextButton::buttonColourId, rOn ? juce::Colour(0xFF383838) : juce::Colour(0xFF222222));
    reverbEnableBtn.setColour(juce::TextButton::buttonOnColourId, rOn ? juce::Colour(0xFF383838) : juce::Colour(0xFF222222));
    reverbEnableBtn.setColour(juce::TextButton::textColourOnId, juce::Colour(0xFFFFFFFF));
    reverbEnableBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xFF777777));
    reverbSizeSlider.setEnabled(rOn);
    reverbSizeSlider.setAlpha(rOn ? 1.0f : 0.35f);
    reverbDampSlider.setEnabled(rOn);
    reverbDampSlider.setAlpha(rOn ? 1.0f : 0.35f);
    reverbMixSlider.setEnabled(rOn);
    reverbMixSlider.setAlpha(rOn ? 1.0f : 0.35f);

    repaint();
}

void WaveSlaveAudioProcessorEditor::mouseDown(const juce::MouseEvent&)
{
    // Deselect partial when clicking empty space
    partialsContainer.setSelectedPartial(-1);
    spectrograph.setSelectedPartial(-1);
}

void WaveSlaveAudioProcessorEditor::saveProfile()
{
    auto userDocs = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    auto profilesDir = userDocs.getChildFile("WaveSlave").getChildFile("Profiles");
    if (!profilesDir.exists())
        profilesDir.createDirectory();

    fileChooser = std::make_unique<juce::FileChooser>(
        "Save Profile",
        profilesDir.exists() ? profilesDir : userDocs,
        "*.waveslave;*.xml");

    auto folderChooserFlags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles;
    fileChooser->launchAsync(folderChooserFlags, [this](const juce::FileChooser& chooser)
    {
        juce::File result = chooser.getResult();
        if (result == juce::File{})
            return;

        if (!result.hasFileExtension(".waveslave") && !result.hasFileExtension(".xml"))
            result = result.withFileExtension("waveslave");

        if (result.existsAsFile())
            result.deleteFile();

        juce::XmlElement profileXml("WaveSlaveProfile");
        profileXml.setAttribute("version", "1.1");
        juce::String profName = result.getFileNameWithoutExtension();
        profileXml.setAttribute("name", profName);

        // 1. APVTS parameter state (includes all 16 partials, master gain, pan, filters, voice, ADSR, velocity)
        auto state = audioProcessor.apvts.copyState();
        if (auto paramXml = state.createXml())
            profileXml.addChildElement(paramXml.release());

        // 2. Custom Wavetables (all 4 slots: sample data + spline control points)
        auto* wtXml = profileXml.createNewChildElement("CustomWavetables");
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

        // 3. Per-partial keyframe animations
        audioProcessor.serializeAnimationsToXml(profileXml);

        // 4. UI / Profile metadata
        auto* uiXml = profileXml.createNewChildElement("UIState");
        int activeNum = (int)audioProcessor.apvts.getRawParameterValue("NUM_PARTIALS")->load();
        uiXml->setAttribute("activePartials", activeNum);
        uiXml->setAttribute("timelineOpen", isTimelineOpen);
        uiXml->setAttribute("scopeOpen", isScopeOpen);
        uiXml->setAttribute("spectrographOpen", isSpecOpen);
        uiXml->setAttribute("masterPanel", (int)currentMasterPanel);
        uiXml->setAttribute("activeCustomSlot", oscilloscope.getSlot());

        // Partial minimized states
        juce::String minStates;
        const auto& strips = partialsContainer.getStrips();
        for (int p = 0; p < strips.size(); ++p)
        {
            if (p > 0) minStates << ",";
            minStates << (strips[p]->getIsMinimized() ? "1" : "0");
        }
        uiXml->setAttribute("minimizedPartials", minStates);

        if (profileXml.writeTo(result))
        {
            factoryPresetBox.setTextWhenNothingSelected(profName);
            factoryPresetBox.setSelectedId(0, juce::dontSendNotification);
            recordPresetSnapshot();
        }
    });
}

void WaveSlaveAudioProcessorEditor::loadProfile()
{
    auto userDocs = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    auto profilesDir = userDocs.getChildFile("WaveSlave").getChildFile("Profiles");

    fileChooser = std::make_unique<juce::FileChooser>(
        "Load Profile",
        profilesDir.exists() ? profilesDir : userDocs,
        "*.waveslave;*.xml");

    auto folderChooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    fileChooser->launchAsync(folderChooserFlags, [this](const juce::FileChooser& chooser)
    {
        juce::File result = chooser.getResult();
        if (!result.existsAsFile())
            return;

        std::unique_ptr<juce::XmlElement> rootXml(juce::XmlDocument::parse(result));
        if (rootXml == nullptr)
            return;

        isApplyingPreset = true;

        juce::XmlElement* paramXml = nullptr;
        juce::XmlElement* wtXml = nullptr;

        if (rootXml->hasTagName("WaveSlaveProfile") || rootXml->hasTagName("WaveSlavePreset"))
        {
            paramXml = rootXml->getChildByName(audioProcessor.apvts.state.getType().toString());
            wtXml = rootXml->getChildByName("CustomWavetables");
        }
        else if (rootXml->hasTagName(audioProcessor.apvts.state.getType()))
        {
            paramXml = rootXml.get();
            wtXml = rootXml->getChildByName("CustomWavetables");
        }

        // 1. APVTS parameters
        if (paramXml != nullptr)
        {
            audioProcessor.apvts.replaceState(juce::ValueTree::fromXml(*paramXml));
        }

        // 2. Animations (keyframes)
        audioProcessor.deserializeAnimationsFromXml(*rootXml);
        if (paramXml != nullptr && paramXml != rootXml.get() && rootXml->getChildByName("PartialAnimations") == nullptr)
        {
            audioProcessor.deserializeAnimationsFromXml(*paramXml);
        }

        // 3. Custom wavetables and spline points
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
                        std::vector<juce::Point<float>> splineVec;
                        auto tokens = juce::StringArray::fromTokens(ptStr, ";", "");
                        for (auto& tok : tokens)
                        {
                            auto xy = juce::StringArray::fromTokens(tok, ",", "");
                            if (xy.size() == 2)
                                splineVec.push_back({ xy[0].getFloatValue(), xy[1].getFloatValue() });
                        }
                        oscilloscope.setSlotSplinePoints(s, splineVec);
                    }
                }
            }
        }

        // 4. UI / Profile state
        auto* uiXml = rootXml->getChildByName("UIState");
        if (uiXml != nullptr)
        {
            if (uiXml->hasAttribute("timelineOpen"))
                isTimelineOpen = uiXml->getBoolAttribute("timelineOpen", true);
            if (uiXml->hasAttribute("scopeOpen"))
                isScopeOpen = uiXml->getBoolAttribute("scopeOpen", true);
            if (uiXml->hasAttribute("spectrographOpen"))
                isSpecOpen = uiXml->getBoolAttribute("spectrographOpen", true);
            if (uiXml->hasAttribute("masterPanel"))
                currentMasterPanel = (SideBarTool::MasterPanel)uiXml->getIntAttribute("masterPanel", (int)SideBarTool::MasterPanel::Envelope);
            if (uiXml->hasAttribute("activeCustomSlot"))
                oscilloscope.setSlot(uiXml->getIntAttribute("activeCustomSlot", 0));

            // Restore minimized partial states
            juce::String minStates = uiXml->getStringAttribute("minimizedPartials");
            if (minStates.isNotEmpty())
            {
                auto tokens = juce::StringArray::fromTokens(minStates, ",", "");
                auto& strips = partialsContainer.getStrips();
                for (int p = 0; p < strips.size() && p < tokens.size(); ++p)
                {
                    strips[p]->setMinimized(tokens[p].getIntValue() == 1);
                }
            }
        }

        isApplyingPreset = false;

        // Sync all UI controls and partial count
        int activeNum = (int)audioProcessor.apvts.getRawParameterValue("NUM_PARTIALS")->load();
        activeNum = juce::jlimit(1, 16, activeNum);
        partialsComboBox.setSelectedId(activeNum, juce::dontSendNotification);
        partialsContainer.setActiveCount(activeNum);
        partialsContainer.refreshAllStrips();
        updatePartialsLayout(activeNum);
        updateVoiceDesc();
        updateFilterButtonsText();
        updateFxButtonsText();

        regenerateButton.setVisible(false);

        juce::String profName = rootXml->getStringAttribute("name", result.getFileNameWithoutExtension());
        if (profName.isEmpty())
            profName = result.getFileNameWithoutExtension();

        factoryPresetBox.setSelectedId(0, juce::dontSendNotification);
        factoryPresetBox.setTextWhenNothingSelected(profName);

        resized();
        recordPresetSnapshot();
        spectrograph.syncTimelineControlsToSelectedPartial();
        oscilloscope.repaint();
        spectrograph.repaint();
    });
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
    currentPresetId = index;
    isApplyingPreset = true;
    audioProcessor.allNotesOff();

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
        float ratios[4]  = { 1.0f,  2.0f,  3.0f,  4.0f };
        float gains[4]   = { 1.0f,  0.75f, 0.55f, 0.40f };
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
            setParam(ap, "DETUNE"      + juce::String(i), 0.0f);
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

        float ratios[6]  = { 1.0f,  2.0f,  3.0f,  4.0f,  5.0f,  6.0f };
        float gains[6]   = { 1.0f,  0.80f, 0.45f, 0.35f, 0.20f, 0.12f };
        float waves[6]   = { 2.0f,  4.0f,  2.0f,  4.0f,  1.0f,  0.0f }; // Saw, Pulse, Saw, Pulse, Tri, Sine
        float detunes[6] = { 0.0f,  5.0f, -4.0f,  3.0f, -2.0f,  1.0f };
        float pans[6]    = { 0.0f, -0.3f,  0.3f, -0.4f,  0.4f,  0.0f };

        for (int i = 1; i <= 6; ++i) {
            setParam(ap, "WAVEFORM"    + juce::String(i), waves[i-1]);
            setParam(ap, "RATIO"       + juce::String(i), ratios[i-1]);
            setParam(ap, "GAIN"        + juce::String(i), gains[i-1]);
            setParam(ap, "DETUNE"      + juce::String(i), 0.0f);
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
            setParam(ap, "P_DECAY"     + juce::String(i), 0.35f);
            setParam(ap, "P_SUSTAIN"   + juce::String(i), 0.70f);
            setParam(ap, "P_RELEASE"   + juce::String(i), 0.15f);
            setParam(ap, "MOD_TARGET"  + juce::String(i), 0.0f); // None
            setParam(ap, "MOD_DEPTH"   + juce::String(i), 0.0f);
            setParam(ap, "MOD_RATE"    + juce::String(i), 2.0f);
            setParam(ap, "MOD_WAVEFORM"+ juce::String(i), 0.0f);
            setParam(ap, "ANIM_MODE"   + juce::String(i), 0.0f);
            setParam(ap, "ANIM_CURVE"  + juce::String(i), 0.0f);
            setParam(ap, "ANIM_DEPTH"  + juce::String(i), 1.0f);
            setParam(ap, "ANIM_DURATION"+ juce::String(i), 2.0f);
        }
        audioProcessor.clearAllKeyframes();
        for (int i = 5; i <= 16; ++i)
            setParam(ap, "GAIN" + juce::String(i), 0.0f);

        setParam(ap, "ATTACK",      0.002f);
        setParam(ap, "DECAY",       0.35f);
        setParam(ap, "SUSTAIN",     0.70f);
        setParam(ap, "RELEASE",     0.15f);
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
        float ratios[8]  = { 1.0f,  2.0f,  3.0f,  4.0f,  7.0f,  9.85f, 14.0f, 18.2f };
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
            setParam(ap, "DETUNE"      + juce::String(i), 0.0f);
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
        float ratios[8]  = { 1.0f,  2.0f,  3.0f,  4.0f,  5.0f,  6.0f,  7.0f,  8.0f };
        float gains[8]   = { 1.0f,  0.85f, 0.60f, 0.50f, 0.35f, 0.30f, 0.18f, 0.14f };
        float detunes[8] = { 0.0f,  8.0f, -8.0f,  6.0f, -6.0f,  4.0f, -4.0f,  2.0f };
        float pans[8]    = {-0.2f,  0.2f, -0.6f,  0.6f, -0.4f,  0.4f, -0.7f,  0.7f };

        for (int i = 1; i <= 8; ++i) {
            setParam(ap, "WAVEFORM"    + juce::String(i), 2.0f); // Saw
            setParam(ap, "RATIO"       + juce::String(i), ratios[i-1]);
            setParam(ap, "GAIN"        + juce::String(i), gains[i-1]);
            setParam(ap, "DETUNE"      + juce::String(i), 0.0f);
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
            setParam(ap, "DETUNE"      + juce::String(i), 0.0f);
            setParam(ap, "PAN"         + juce::String(i), pans[i-1]);
            setParam(ap, "P_ADSR_ENABLED" + juce::String(i), (i >= 2 ? 1.0f : 0.0f)); // Damping on upper harmonics
            setParam(ap, "P_ATTACK"    + juce::String(i), 0.001f);
            setParam(ap, "P_DECAY"     + juce::String(i), pDecays[i-1]);
            setParam(ap, "P_SUSTAIN"   + juce::String(i), (i == 1 ? 0.20f : 0.0f));
            setParam(ap, "P_RELEASE"   + juce::String(i), 0.80f);
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
        setParam(ap, "DECAY",       0.80f);
        setParam(ap, "SUSTAIN",     0.15f);
        setParam(ap, "RELEASE",     0.80f);
        setParam(ap, "MASTER_GAIN", 0.54f);
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
            setParam(ap, "DETUNE"      + juce::String(i), 0.0f);
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
            setParam(ap, "DETUNE"      + juce::String(i), 0.0f);
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
            setParam(ap, "DETUNE"      + juce::String(i), 0.0f);
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
    else if (index == 13) // Alien Drone - restored to 4 partials (classic inharmonic saw drone)
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
            setParam(ap, "DETUNE"      + juce::String(i), 0.0f);
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
    else if (index == 14) // Soft Pad - warm analog poly-pad standard
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
            setParam(ap, "DETUNE"      + juce::String(i), 0.0f);
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
    else if (index == 15) // Seraphim - Angelic celestial choir pad, warm sub foundation
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
        setParam(ap, "DETUNE2",       0.0f);
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
        setParam(ap, "DETUNE3",       0.0f);
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
        setParam(ap, "DETUNE4",       0.0f);
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
        setParam(ap, "DETUNE5",       0.0f);
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
    else if (index == 16) // 808 Drum Bass - Classic punchy analog sub kick & booming gliding bass
    {
        setParam(ap, "NUM_PARTIALS", 3.0f);
        setParam(ap, "VOICE_MODE",   1.0f); // Mono (classic 808 mono bass)
        setParam(ap, "GLIDE_TIME",   0.04f); // Smooth pitch slides between notes
        setParam(ap, "BASE_NOTE",    36.0f); // C2 sub foundation
        setParam(ap, "VELOCITY_ENABLED", 1.0f);

        // Clear all partial animation tracks - authentic 808 does not use keyframe animation
        audioProcessor.clearAllKeyframes();

        // Partial 1: The Sub Boom
        setParam(ap, "WAVEFORM1",     0.0f); // Pure Sine
        setParam(ap, "RATIO1",        1.0f); // Sub Fundamental
        setParam(ap, "SEMITONE1",     0.0f);
        setParam(ap, "GAIN1",         1.0f); // Full power
        setParam(ap, "DETUNE1",       0.0f);
        setParam(ap, "PAN1",          0.0f);
        setParam(ap, "MOD_TARGET1",   0.0f); // No LFO
        setParam(ap, "ANIM_MODE1",    0.0f); // No Animation
        setParam(ap, "ANIM_CURVE1",   0.0f);
        setParam(ap, "ANIM_DEPTH1",   1.0f);
        setParam(ap, "P_ADSR_ENABLED1", 1.0f);
        setParam(ap, "P_ATTACK1",     0.002f); // Snappy instant attack
        setParam(ap, "P_DECAY1",      2.0f);   // Long booming decay
        setParam(ap, "P_SUSTAIN1",    0.20f);  // Sustained body
        setParam(ap, "P_RELEASE1",    0.35f);

        // Partial 2: 2nd Harmonic (Octave 2.0x) - Saturation / speaker body
        setParam(ap, "WAVEFORM2",     0.0f); // Pure Sine
        setParam(ap, "RATIO2",        2.0f); // Octave overtone
        setParam(ap, "SEMITONE2",     0.0f);
        setParam(ap, "GAIN2",         0.22f);
        setParam(ap, "DETUNE2",       0.0f);
        setParam(ap, "PAN2",          0.0f);
        setParam(ap, "MOD_TARGET2",   0.0f);
        setParam(ap, "ANIM_MODE2",    0.0f);
        setParam(ap, "P_ADSR_ENABLED2", 1.0f);
        setParam(ap, "P_ATTACK2",     0.002f);
        setParam(ap, "P_DECAY2",      0.50f); // Decays quickly, giving punch then leaving clean sub
        setParam(ap, "P_SUSTAIN2",    0.04f);
        setParam(ap, "P_RELEASE2",    0.20f);

        // Partial 3: 3rd Harmonic (3.0x) - Transient punch
        setParam(ap, "WAVEFORM3",     0.0f); // Sine
        setParam(ap, "RATIO3",        3.0f); // 3rd harmonic
        setParam(ap, "SEMITONE3",     0.0f);
        setParam(ap, "GAIN3",         0.10f);
        setParam(ap, "DETUNE3",       0.0f);
        setParam(ap, "PAN3",          0.0f);
        setParam(ap, "MOD_TARGET3",   0.0f);
        setParam(ap, "ANIM_MODE3",    0.0f);
        setParam(ap, "P_ADSR_ENABLED3", 1.0f);
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
    else if (index == 17) // Random - procedurally generated patch from scratch
    {
        audioProcessor.clearAllKeyframes();
        oscilloscope.clearWavetable();

        juce::Random rng (juce::Time::getHighResolutionTicks());

        // Archetypes: 0 = Pluck, 1 = Pad, 2 = Lead, 3 = Bass, 4 = Bell, 5 = Keys, 6 = Drone
        int archetype = rng.nextInt(7);

        // 1. Master Envelope, Voicing & Filters generated from scratch
        float masterAtk = 0.01f, masterDec = 0.5f, masterSus = 0.5f, masterRel = 0.5f;
        float voiceMode = 0.0f; // 0=Poly, 1=Mono, 2=Legato
        float glideTime = 0.0f;
        float baseNote  = 60.0f; // C4 default

        float lpfEnabled = 0.0f;
        float lpfCutoff  = 20000.0f;
        float lpfReso    = 0.707f;
        float hpfEnabled = 0.0f;
        float hpfCutoff  = 20.0f;
        float hpfReso    = 0.707f;

        int count = 4;

        if (archetype == 0) // Pluck
        {
            count = 3 + rng.nextInt(4); // 3 - 6 partials
            baseNote = 60.0f;
            voiceMode = 0.0f; // Poly
            masterAtk = 0.002f + rng.nextFloat() * 0.006f;
            masterDec = 0.25f + rng.nextFloat() * 0.55f;
            masterSus = 0.0f;
            masterRel = 0.20f + rng.nextFloat() * 0.35f;

            lpfEnabled = 1.0f;
            lpfCutoff  = 3000.0f + rng.nextFloat() * 7000.0f;
            lpfReso    = 1.0f + rng.nextFloat() * 0.8f;
        }
        else if (archetype == 1) // Pad / Ambient
        {
            count = 5 + rng.nextInt(4); // 5 - 8 partials
            baseNote = 60.0f;
            voiceMode = 0.0f; // Poly
            masterAtk = 0.40f + rng.nextFloat() * 1.20f;
            masterDec = 1.50f + rng.nextFloat() * 1.50f;
            masterSus = 0.70f + rng.nextFloat() * 0.25f;
            masterRel = 1.50f + rng.nextFloat() * 1.80f;

            hpfEnabled = 1.0f;
            hpfCutoff  = 30.0f + rng.nextFloat() * 30.0f;
            lpfEnabled = 1.0f;
            lpfCutoff  = 2500.0f + rng.nextFloat() * 5000.0f;
            lpfReso    = 0.707f + rng.nextFloat() * 0.5f;
        }
        else if (archetype == 2) // Lead
        {
            count = 3 + rng.nextInt(4); // 3 - 6 partials
            baseNote = (rng.nextFloat() < 0.35f) ? 48.0f : 60.0f;
            voiceMode = (rng.nextFloat() < 0.65f) ? 1.0f : 0.0f; // Mostly Mono
            glideTime = (voiceMode > 0.0f) ? (0.02f + rng.nextFloat() * 0.06f) : 0.0f;
            masterAtk = 0.005f + rng.nextFloat() * 0.035f;
            masterDec = 0.30f + rng.nextFloat() * 0.50f;
            masterSus = 0.60f + rng.nextFloat() * 0.25f;
            masterRel = 0.25f + rng.nextFloat() * 0.40f;

            lpfEnabled = 1.0f;
            lpfCutoff  = 2200.0f + rng.nextFloat() * 4500.0f;
            lpfReso    = 1.4f + rng.nextFloat() * 1.4f;
        }
        else if (archetype == 3) // Bass
        {
            count = 3 + rng.nextInt(3); // 3 - 5 partials
            baseNote = (rng.nextFloat() < 0.5f) ? 36.0f : 48.0f; // C2 or C3
            voiceMode = (rng.nextFloat() < 0.8f) ? 1.0f : 2.0f; // Mono or Legato
            glideTime = 0.02f + rng.nextFloat() * 0.05f;
            masterAtk = 0.002f + rng.nextFloat() * 0.015f;
            masterDec = 0.40f + rng.nextFloat() * 0.80f;
            masterSus = 0.20f + rng.nextFloat() * 0.45f;
            masterRel = 0.15f + rng.nextFloat() * 0.35f;

            hpfEnabled = 1.0f;
            hpfCutoff  = 24.0f + rng.nextFloat() * 15.0f;
            lpfEnabled = 1.0f;
            lpfCutoff  = 1200.0f + rng.nextFloat() * 2500.0f;
            lpfReso    = 1.2f + rng.nextFloat() * 1.8f;
        }
        else if (archetype == 4) // Bell / Chime
        {
            count = 4 + rng.nextInt(4); // 4 - 7 partials
            baseNote = 60.0f;
            voiceMode = 0.0f;
            masterAtk = 0.001f + rng.nextFloat() * 0.004f;
            masterDec = 0.80f + rng.nextFloat() * 1.40f;
            masterSus = 0.05f + rng.nextFloat() * 0.15f;
            masterRel = 0.80f + rng.nextFloat() * 1.20f;

            hpfEnabled = 1.0f;
            hpfCutoff  = 40.0f;
            lpfEnabled = (rng.nextFloat() < 0.5f) ? 1.0f : 0.0f;
            lpfCutoff  = 6000.0f + rng.nextFloat() * 8000.0f;
            lpfReso    = 0.707f;
        }
        else if (archetype == 5) // Keys / Vintage EP
        {
            count = 4 + rng.nextInt(3); // 4 - 6 partials
            baseNote = 60.0f;
            voiceMode = 0.0f;
            masterAtk = 0.005f + rng.nextFloat() * 0.020f;
            masterDec = 0.70f + rng.nextFloat() * 0.80f;
            masterSus = 0.40f + rng.nextFloat() * 0.30f;
            masterRel = 0.35f + rng.nextFloat() * 0.50f;

            lpfEnabled = 1.0f;
            lpfCutoff  = 3500.0f + rng.nextFloat() * 4500.0f;
            lpfReso    = 0.9f + rng.nextFloat() * 0.6f;
        }
        else // Drone / Sci-Fi
        {
            count = 4 + rng.nextInt(5); // 4 - 8 partials
            baseNote = (rng.nextFloat() < 0.5f) ? 36.0f : 48.0f;
            voiceMode = 0.0f;
            masterAtk = 1.0f + rng.nextFloat() * 1.5f;
            masterDec = 1.8f + rng.nextFloat() * 1.5f;
            masterSus = 0.85f + rng.nextFloat() * 0.15f;
            masterRel = 2.0f + rng.nextFloat() * 2.0f;

            hpfEnabled = 1.0f;
            hpfCutoff  = 30.0f;
            lpfEnabled = 1.0f;
            lpfCutoff  = 1800.0f + rng.nextFloat() * 3200.0f;
            lpfReso    = 1.1f + rng.nextFloat() * 0.9f;
        }

        setParam(ap, "NUM_PARTIALS", (float)count);
        setParam(ap, "BASE_NOTE",    baseNote);
        setParam(ap, "VOICE_MODE",   voiceMode);
        setParam(ap, "GLIDE_TIME",   glideTime);
        setParam(ap, "ATTACK",       masterAtk);
        setParam(ap, "DECAY",        masterDec);
        setParam(ap, "SUSTAIN",      masterSus);
        setParam(ap, "RELEASE",      masterRel);
        setParam(ap, "MASTER_GAIN",  0.50f);
        setParam(ap, "MASTER_PAN",   0.0f);
        setParam(ap, "VELOCITY_ENABLED", 1.0f);
        setParam(ap, "BASE_WAVEFORM", 0.0f);

        setParam(ap, "HPF_ENABLED",  hpfEnabled);
        setParam(ap, "HPF_CUTOFF",   hpfCutoff);
        setParam(ap, "HPF_RESO",     hpfReso);
        setParam(ap, "LPF_ENABLED",  lpfEnabled);
        setParam(ap, "LPF_CUTOFF",   lpfCutoff);
        setParam(ap, "LPF_RESO",     lpfReso);

        // Animation selection: enabled on ~55% of patches, strictly on 1 or 2 upper partials
        bool enableAnimOnPatch = (rng.nextFloat() < 0.55f);
        int animTarget1 = -1;
        int animTarget2 = -1;
        if (enableAnimOnPatch && count >= 2)
        {
            animTarget1 = 2 + rng.nextInt(count - 1);
            if (count >= 5 && rng.nextFloat() < 0.25f)
            {
                animTarget2 = 2 + rng.nextInt(count - 1);
                if (animTarget2 == animTarget1)
                    animTarget2 = (animTarget1 % (count - 1)) + 2;
            }
        }

        // 2. Generate all 16 partials from scratch
        for (int i = 1; i <= 16; ++i)
        {
            juce::String s = juce::String(i);
            if (i <= count)
            {
                // Waveform selection per archetype
                float wave = 0.0f;
                if (archetype == 3) // Bass: pure sine base, saw/pulse/square tops
                    wave = (i == 1) ? 0.0f : (float)(rng.nextInt(5));
                else if (archetype == 1) // Pad
                    wave = (float)(rng.nextInt(3)); // Sine, Tri, Saw
                else if (archetype == 4) // Bell
                    wave = (float)((rng.nextInt(2) == 0) ? 0 : (rng.nextInt(2) == 0 ? 5 : 6)); // Sine, Sharkfin, Fibonacci
                else
                    wave = (float)(rng.nextInt(7));

                // Ratio selection
                float ratio = (float)i;
                if (archetype == 4) // Bell inharmonic intervals
                {
                    float bellRatios[8] = { 1.0f, 1.414f, 2.0f, 2.76f, 3.0f, 4.15f, 5.0f, 6.28f };
                    ratio = bellRatios[juce::jmin(i - 1, 7)];
                }
                else if (archetype == 6) // Drone
                {
                    ratio = 1.0f + 0.5f * (float)(i - 1);
                }
                else if (i == 1 && (archetype == 3 || archetype == 6) && rng.nextFloat() < 0.35f)
                {
                    ratio = 0.5f; // Sub-octave fundamental
                }

                // Semitone: strictly 0 on root, mostly 0 on uppers with rare 5th or octave
                float semi = 0.0f;
                if (i > 1 && rng.nextFloat() < 0.15f)
                    semi = (rng.nextFloat() < 0.5f) ? 7.0f : 12.0f;

                // Detune: subtle analog chorusing on upper partials
                float detune = (i == 1) ? 0.0f : (-3.5f + rng.nextFloat() * 7.0f);

                // Gain: organic taper
                float baseGain = (i == 1) ? 1.0f : juce::jlimit(0.08f, 0.85f, (0.45f + rng.nextFloat() * 0.45f) / std::pow((float)i, 0.75f));

                // Pan: stereo dispersion
                float pan = (i == 1) ? 0.0f : ((i % 2 == 0 ? 0.45f : -0.45f) * ((float)i / (float)count) * (0.6f + rng.nextFloat() * 0.4f));

                // ADSR: mostly tracks master envelope cleanly
                float pAtk = masterAtk * (0.85f + rng.nextFloat() * 0.30f);
                float pDec = masterDec * (0.85f + rng.nextFloat() * 0.30f);
                float pSus = juce::jlimit(0.0f, 1.0f, masterSus * (0.90f + rng.nextFloat() * 0.20f));
                float pRel = masterRel * (0.85f + rng.nextFloat() * 0.30f);

                setParam(ap, "WAVEFORM"       + s, wave);
                setParam(ap, "RATIO"          + s, ratio);
                setParam(ap, "SEMITONE"       + s, semi);
                setParam(ap, "DETUNE"         + s, detune);
                setParam(ap, "GAIN"           + s, baseGain);
                setParam(ap, "PAN"            + s, pan);
                setParam(ap, "P_ADSR_ENABLED" + s, 0.0f);
                setParam(ap, "P_ATTACK"       + s, pAtk);
                setParam(ap, "P_DECAY"        + s, pDec);
                setParam(ap, "P_SUSTAIN"      + s, pSus);
                setParam(ap, "P_RELEASE"      + s, pRel);
                setParam(ap, "P_GLIDE"        + s, 0.0f);

                // LFO Modulation:
                if (i > 1 && rng.nextFloat() < 0.45f)
                {
                    // Target: 1=Pitch (rare/subtle), 2=Level, 3=Pan
                    float modTarget = (rng.nextFloat() < 0.60f) ? 3.0f : ((rng.nextFloat() < 0.70f) ? 2.0f : 1.0f);
                    float modRate   = 0.3f + rng.nextFloat() * 2.2f;
                    float modDepth  = (modTarget == 1.0f) ? (0.012f + rng.nextFloat() * 0.010f)
                                    : (modTarget == 2.0f) ? (0.20f + rng.nextFloat() * 0.25f)
                                                          : (0.40f + rng.nextFloat() * 0.40f);
                    float modWave   = (float)(rng.nextInt(2)); // Sine or Tri
                    setParam(ap, "MOD_TARGET"   + s, modTarget);
                    setParam(ap, "MOD_RATE"     + s, modRate);
                    setParam(ap, "MOD_DEPTH"    + s, modDepth);
                    setParam(ap, "MOD_WAVEFORM" + s, modWave);
                    setParam(ap, "MOD_PHASE"    + s, (float)((i * 47) % 360));
                }
                else
                {
                    setParam(ap, "MOD_TARGET"   + s, 0.0f);
                    setParam(ap, "MOD_DEPTH"    + s, 0.0f);
                    setParam(ap, "MOD_RATE"     + s, 2.0f);
                    setParam(ap, "MOD_WAVEFORM" + s, 0.0f);
                    setParam(ap, "MOD_PHASE"    + s, 0.0f);
                }

                // Keyframe Animation
                bool isAnimated = (i == animTarget1 || i == animTarget2);
                if (isAnimated)
                {
                    int aMode = (archetype == 0) ? 1 : ((rng.nextInt(2) == 0) ? 3 : 4);
                    float aDur = 2.0f + rng.nextFloat() * 2.5f;
                    float aDepth = 0.35f + rng.nextFloat() * 0.25f;
                    setParam(ap, "ANIM_MODE"     + s, (float)aMode);
                    setParam(ap, "ANIM_CURVE"    + s, 0.0f); // Smooth Hermite
                    setParam(ap, "ANIM_DURATION" + s, aDur);
                    setParam(ap, "ANIM_DEPTH"    + s, aDepth);

                    PartialAnimationData track;
                    float r0 = ratio;
                    float g0 = baseGain;
                    float p0 = pan;
                    float pDrift = (p0 >= 0.0f) ? -0.35f : 0.35f;
                    track.keyframes[0] = { 0.0f,  r0, g0,                                 0.0f, 0.0f, p0 };
                    track.keyframes[1] = { 0.33f, r0, juce::jlimit(0.05f, 1.0f, g0 * 0.70f), 0.0f, 0.0f, juce::jlimit(-1.0f, 1.0f, p0 + pDrift * 0.5f) };
                    track.keyframes[2] = { 0.66f, r0, juce::jlimit(0.05f, 1.0f, g0 * 1.20f), 0.0f, 0.0f, juce::jlimit(-1.0f, 1.0f, p0 + pDrift) };
                    track.keyframes[3] = { 1.0f,  r0, g0,                                 0.0f, 0.0f, p0 };
                    track.numKeyframes = 4;
                    audioProcessor.setPartialAnimation(i - 1, track);
                }
                else
                {
                    setParam(ap, "ANIM_MODE"     + s, 0.0f);
                    setParam(ap, "ANIM_CURVE"    + s, 0.0f);
                    setParam(ap, "ANIM_DURATION" + s, 2.0f);
                    setParam(ap, "ANIM_DEPTH"    + s, 0.0f);
                }
            }
            else
            {
                // Inactive partial: completely reset every parameter
                setParam(ap, "GAIN"           + s, 0.0f);
                setParam(ap, "WAVEFORM"       + s, 0.0f);
                setParam(ap, "RATIO"          + s, (float)i);
                setParam(ap, "SEMITONE"       + s, 0.0f);
                setParam(ap, "DETUNE"         + s, 0.0f);
                setParam(ap, "PAN"            + s, 0.0f);
                setParam(ap, "P_ADSR_ENABLED" + s, 0.0f);
                setParam(ap, "P_ATTACK"       + s, 0.01f);
                setParam(ap, "P_DECAY"        + s, 0.3f);
                setParam(ap, "P_SUSTAIN"      + s, 1.0f);
                setParam(ap, "P_RELEASE"      + s, 0.5f);
                setParam(ap, "P_GLIDE"        + s, 0.0f);
                setParam(ap, "MOD_TARGET"     + s, 0.0f);
                setParam(ap, "MOD_DEPTH"      + s, 0.0f);
                setParam(ap, "MOD_RATE"       + s, 2.0f);
                setParam(ap, "MOD_WAVEFORM"   + s, 0.0f);
                setParam(ap, "MOD_PHASE"      + s, 0.0f);
                setParam(ap, "ANIM_MODE"      + s, 0.0f);
                setParam(ap, "ANIM_CURVE"     + s, 0.0f);
                setParam(ap, "ANIM_DURATION"  + s, 2.0f);
                setParam(ap, "ANIM_DEPTH"     + s, 0.0f);
            }
        }
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

    for (int i = 0; i < 16; ++i)
        spectrograph.recordKeyframeFromSliders(i);
    audioProcessor.syncLivePartialStateFromParams();
    spectrograph.syncTimelineControlsToSelectedPartial();
    spectrograph.repaint();
    oscilloscope.repaint();

    recordPresetSnapshot();

    juce::Component::SafePointer<WaveSlaveAudioProcessorEditor> safeThis(this);
    juce::MessageManager::callAsync([safeThis]() {
        if (safeThis != nullptr)
        {
            safeThis->recordPresetSnapshot();
            safeThis->isApplyingPreset = false;
        }
    });

    if (index == 1) // Reset
    {
        factoryPresetBox.setSelectedId(1, juce::dontSendNotification);
        regenerateButton.setVisible(false);
        resized();
    }
    else if (index == 17) // Random
    {
        factoryPresetBox.setSelectedId(17, juce::dontSendNotification);
        regenerateButton.setVisible(true);
        resized();
    }
    else
    {
        factoryPresetBox.setSelectedId(index, juce::dontSendNotification);
        regenerateButton.setVisible(false);
        resized();
    }
}

void WaveSlaveAudioProcessorEditor::resized()
{
    audioProcessor.editorWidth = getWidth();
    audioProcessor.editorHeight = getHeight();

    auto area = getLocalBounds();

    // Top Header row across full width
    auto headerArea = area.removeFromTop(36).reduced(10, 4);

    // Left side: Title + File & Edit menus
    titleLabel.setBounds(headerArea.removeFromLeft(125));
    headerArea.removeFromLeft(8);
    fileMenuBox.setBounds(headerArea.removeFromLeft(68));
    headerArea.removeFromLeft(4);
    editMenuBox.setBounds(headerArea.removeFromLeft(68));

    // Right side: Partials count dropdown + Preset selector with < and > buttons
    partialsComboBox.setBounds(headerArea.removeFromRight(115));
    headerArea.removeFromRight(6);
    nextPresetBtn.setBounds(headerArea.removeFromRight(22));
    headerArea.removeFromRight(2);
    factoryPresetBox.setBounds(headerArea.removeFromRight(140));
    headerArea.removeFromRight(2);
    prevPresetBtn.setBounds(headerArea.removeFromRight(22));
    if (regenerateButton.isVisible())
    {
        headerArea.removeFromRight(4);
        int btnSize = headerArea.getHeight();
        regenerateButton.setBounds(headerArea.removeFromRight(btnSize));
    }

    // Main workspace below header
    auto mainWorkspace = area.reduced(10);

    // Sidebar tool docked on the left
    auto sidebarArea = mainWorkspace.removeFromLeft(54);
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
        bool isCompact = !isTallMode;

        // Hide all panel controls by default
        mainEnvLabel.setText("ENVELOPE", juce::dontSendNotification);
        mainEnvLabel.setVisible(false);
        susRelEnvLabel.setVisible(false);
        attackVSlider.setVisible(false);
        decayVSlider.setVisible(false);
        sustainVSlider.setVisible(false);
        releaseVSlider.setVisible(false);

        lpfSectionLabel.setVisible(false);
        lpfEnableButton.setVisible(false);
        lpfCutoffSlider.setVisible(false);
        lpfResoSlider.setVisible(false);
        lpfEnvSlider.setVisible(false);
        hpfSectionLabel.setVisible(false);
        hpfEnableButton.setVisible(false);
        hpfCutoffSlider.setVisible(false);
        hpfResoSlider.setVisible(false);
        hpfEnvSlider.setVisible(false);

        voiceSectionLabel.setVisible(false);
        voiceModeLabel.setVisible(false);
        masterVoiceBox.setVisible(false);
        masterGlideSlider.setVisible(false);
        phaseModeLabel.setVisible(false);
        phaseModeBox.setVisible(false);
        pitchBendLabel.setVisible(false);
        pitchBendBox.setVisible(false);
        masterClipperToggle.setVisible(false);
        voiceModeDescLabel.setVisible(false);

        chorusLabel.setVisible(false);
        chorusEnableBtn.setVisible(false);
        chorusRateSlider.setVisible(false);
        chorusDepthSlider.setVisible(false);
        chorusMixSlider.setVisible(false);
        delayLabel.setVisible(false);
        delayEnableBtn.setVisible(false);
        delayTimeSlider.setVisible(false);
        delayFeedbackSlider.setVisible(false);
        delayMixSlider.setVisible(false);
        reverbLabel.setVisible(false);
        reverbEnableBtn.setVisible(false);
        reverbSizeSlider.setVisible(false);
        reverbDampSlider.setVisible(false);
        reverbMixSlider.setVisible(false);

        // --- 1. Top Section: Output, Master & Pan, Level Gauge below them ---
        auto outHeader = masterArea.removeFromTop(15);
        outputLabel.setText("OUTPUT", juce::dontSendNotification);
        outputLabel.setVisible(true);
        outputLabel.setBounds(outHeader);
        masterArea.removeFromTop(2);

        // Master & Pan row (Text to left with value next to label when compact; label on top, value on right when not compact)
        if (isCompact)
        {
            masterGainSlider.setLabelPosition(LabeledSlider::LabelPosition::Left);
            masterGainSlider.setLabelWidth(46);
            masterGainSlider.setTextBoxToRight(false, 42, 14);

            masterPanSlider.setLabelPosition(LabeledSlider::LabelPosition::Left);
            masterPanSlider.setLabelWidth(32);
            masterPanSlider.setTextBoxToRight(false, 38, 14);

            int masterSlidersH = 18;
            auto masterSlidersRow = masterArea.removeFromTop(masterSlidersH);
            masterArea.removeFromTop(5);

            int masterGap = 16;
            int halfMasterW = (masterSlidersRow.getWidth() - masterGap) / 2;
            masterGainSlider.setBounds(masterSlidersRow.removeFromLeft(halfMasterW));
            masterSlidersRow.removeFromLeft(masterGap);
            masterPanSlider.setBounds(masterSlidersRow);
        }
        else
        {
            masterGainSlider.setLabelPosition(LabeledSlider::LabelPosition::Top);
            masterGainSlider.setTextBoxToRight(true, 42, 14);

            masterPanSlider.setLabelPosition(LabeledSlider::LabelPosition::Top);
            masterPanSlider.setTextBoxToRight(true, 38, 14);

            int masterSlidersH = 28;
            auto masterSlidersRow = masterArea.removeFromTop(masterSlidersH);
            masterArea.removeFromTop(5);

            int masterGap = 12;
            int halfMasterW = (masterSlidersRow.getWidth() - masterGap) / 2;
            masterGainSlider.setBounds(masterSlidersRow.removeFromLeft(halfMasterW));
            masterSlidersRow.removeFromLeft(masterGap);
            masterPanSlider.setBounds(masterSlidersRow);
        }

        // Level gauge sits BELOW master and pan settings
        int meterH = 8;
        auto meterRow = masterArea.removeFromTop(meterH);
        peakMeter.setBounds(meterRow);
        masterArea.removeFromTop(isCompact ? 6 : 7);

        // --- 2. Panel Controls Underneath across full width ---
        if (currentMasterPanel == SideBarTool::MasterPanel::FX)
        {
            int fxSpacing = 12;
            int fxColW = (masterArea.getWidth() - 2 * fxSpacing) / 3;
            auto cCol = masterArea.removeFromLeft(fxColW);
            masterArea.removeFromLeft(fxSpacing);
            auto dCol = masterArea.removeFromLeft(fxColW);
            masterArea.removeFromLeft(fxSpacing);
            auto rCol = masterArea;

            auto layoutFxCol = [isCompact](juce::Rectangle<int> colArea, juce::Label& lbl, juce::TextButton& btn,
                                           LabeledSlider& s1, LabeledSlider& s2, LabeledSlider& s3)
            {
                lbl.setVisible(true);
                btn.setVisible(true);
                s1.setVisible(true);
                s2.setVisible(true);
                s3.setVisible(true);

                if (isCompact)
                {
                    s1.setLabelPosition(LabeledSlider::LabelPosition::Left);
                    s1.setLabelWidth(38);
                    s1.setTextBoxToRight(false, 36, 14);

                    s2.setLabelPosition(LabeledSlider::LabelPosition::Left);
                    s2.setLabelWidth(38);
                    s2.setTextBoxToRight(false, 36, 14);

                    s3.setLabelPosition(LabeledSlider::LabelPosition::Left);
                    s3.setLabelWidth(38);
                    s3.setTextBoxToRight(false, 36, 14);
                }
                else
                {
                    s1.setLabelPosition(LabeledSlider::LabelPosition::Top);
                    s1.setTextBoxToRight(true, 36, 14);

                    s2.setLabelPosition(LabeledSlider::LabelPosition::Top);
                    s2.setTextBoxToRight(true, 36, 14);

                    s3.setLabelPosition(LabeledSlider::LabelPosition::Top);
                    s3.setTextBoxToRight(true, 36, 14);
                }

                auto hdr = colArea.removeFromTop(16);
                btn.setBounds(hdr.removeFromRight(32).reduced(0, 1));
                hdr.removeFromRight(4);
                lbl.setBounds(hdr);
                colArea.removeFromTop(isCompact ? 3 : 4);

                int sH = isCompact ? 18 : 28;
                int sGap = isCompact ? 2 : 4;
                s1.setBounds(colArea.removeFromTop(sH));
                colArea.removeFromTop(sGap);
                s2.setBounds(colArea.removeFromTop(sH));
                colArea.removeFromTop(sGap);
                s3.setBounds(colArea.removeFromTop(sH));
            };

            layoutFxCol(cCol, chorusLabel, chorusEnableBtn, chorusRateSlider, chorusDepthSlider, chorusMixSlider);
            layoutFxCol(dCol, delayLabel, delayEnableBtn, delayTimeSlider, delayFeedbackSlider, delayMixSlider);
            layoutFxCol(rCol, reverbLabel, reverbEnableBtn, reverbSizeSlider, reverbDampSlider, reverbMixSlider);
        }
        else if (currentMasterPanel == SideBarTool::MasterPanel::Filters)
        {
            lpfSectionLabel.setVisible(true);
            lpfEnableButton.setVisible(true);
            lpfCutoffSlider.setVisible(true);
            lpfResoSlider.setVisible(true);
            lpfEnvSlider.setVisible(true);

            hpfSectionLabel.setVisible(true);
            hpfEnableButton.setVisible(true);
            hpfCutoffSlider.setVisible(true);
            hpfResoSlider.setVisible(true);
            hpfEnvSlider.setVisible(true);

            int filterSpacing = 16;
            int filterColW = (masterArea.getWidth() - filterSpacing) / 2;
            auto lpfArea = masterArea.removeFromLeft(filterColW);
            masterArea.removeFromLeft(filterSpacing);
            auto hpfArea = masterArea;

            auto layoutFilterCol = [isCompact](juce::Rectangle<int> colArea, juce::Label& lbl, juce::TextButton& btn,
                                                LabeledSlider& cutoff, LabeledSlider& reso, LabeledSlider& env)
            {
                if (isCompact)
                {
                    cutoff.setLabelPosition(LabeledSlider::LabelPosition::Left);
                    cutoff.setLabelWidth(44);
                    cutoff.setTextBoxToRight(false, 38, 14);

                    reso.setLabelPosition(LabeledSlider::LabelPosition::Left);
                    reso.setLabelWidth(44);
                    reso.setTextBoxToRight(false, 38, 14);

                    env.setLabelPosition(LabeledSlider::LabelPosition::Left);
                    env.setLabelWidth(44);
                    env.setTextBoxToRight(false, 38, 14);
                }
                else
                {
                    cutoff.setLabelPosition(LabeledSlider::LabelPosition::Top);
                    cutoff.setTextBoxToRight(true, 38, 14);

                    reso.setLabelPosition(LabeledSlider::LabelPosition::Top);
                    reso.setTextBoxToRight(true, 38, 14);

                    env.setLabelPosition(LabeledSlider::LabelPosition::Top);
                    env.setTextBoxToRight(true, 38, 14);
                }

                auto hdr = colArea.removeFromTop(16);
                btn.setBounds(hdr.removeFromRight(32).reduced(0, 1));
                hdr.removeFromRight(4);
                lbl.setBounds(hdr);
                colArea.removeFromTop(isCompact ? 3 : 4);

                int sH = isCompact ? 18 : 28;
                int sGap = isCompact ? 2 : 4;
                cutoff.setBounds(colArea.removeFromTop(sH));
                colArea.removeFromTop(sGap);
                reso.setBounds(colArea.removeFromTop(sH));
                colArea.removeFromTop(sGap);
                env.setBounds(colArea.removeFromTop(sH));
            };

            layoutFilterCol(lpfArea, lpfSectionLabel, lpfEnableButton, lpfCutoffSlider, lpfResoSlider, lpfEnvSlider);
            layoutFilterCol(hpfArea, hpfSectionLabel, hpfEnableButton, hpfCutoffSlider, hpfResoSlider, hpfEnvSlider);
        }
        else if (currentMasterPanel == SideBarTool::MasterPanel::Envelope)
        {
            // Envelope settings span full width in 2 columns for both modes; 2 row 2 col when not compact
            mainEnvLabel.setText("MASTER ENVELOPE", juce::dontSendNotification);
            mainEnvLabel.setVisible(true);
            attackVSlider.setVisible(true);
            decayVSlider.setVisible(true);
            sustainVSlider.setVisible(true);
            releaseVSlider.setVisible(true);

            auto hdr = masterArea.removeFromTop(15);
            mainEnvLabel.setBounds(hdr);
            masterArea.removeFromTop(isCompact ? 4 : 6);

            int envSpacing = isCompact ? 20 : 16;
            int envColW = (masterArea.getWidth() - envSpacing) / 2;
            auto col1 = masterArea.removeFromLeft(envColW);
            masterArea.removeFromLeft(envSpacing);
            auto col2 = masterArea;

            if (isCompact)
            {
                // Compact: Values NEXT to settings text
                attackVSlider.setLabelPosition(LabeledSlider::LabelPosition::Left);
                attackVSlider.setLabelWidth(50);
                attackVSlider.setTextBoxToRight(false, 42, 14);

                decayVSlider.setLabelPosition(LabeledSlider::LabelPosition::Left);
                decayVSlider.setLabelWidth(50);
                decayVSlider.setTextBoxToRight(false, 42, 14);

                sustainVSlider.setLabelPosition(LabeledSlider::LabelPosition::Left);
                sustainVSlider.setLabelWidth(50);
                sustainVSlider.setTextBoxToRight(false, 42, 14);

                releaseVSlider.setLabelPosition(LabeledSlider::LabelPosition::Left);
                releaseVSlider.setLabelWidth(50);
                releaseVSlider.setTextBoxToRight(false, 42, 14);

                int sH = 18;
                int sGap = 4;

                // Col 1: Attack & Decay
                attackVSlider.setBounds(col1.removeFromTop(sH));
                col1.removeFromTop(sGap);
                decayVSlider.setBounds(col1.removeFromTop(sH));

                // Col 2: Sustain & Release
                sustainVSlider.setBounds(col2.removeFromTop(sH));
                col2.removeFromTop(sGap);
                releaseVSlider.setBounds(col2.removeFromTop(sH));
            }
            else
            {
                // Not compact: 2 row, 2 column. Values to the RIGHT side of slider.
                attackVSlider.setLabelPosition(LabeledSlider::LabelPosition::Top);
                attackVSlider.setTextBoxToRight(true, 42, 14);

                decayVSlider.setLabelPosition(LabeledSlider::LabelPosition::Top);
                decayVSlider.setTextBoxToRight(true, 42, 14);

                sustainVSlider.setLabelPosition(LabeledSlider::LabelPosition::Top);
                sustainVSlider.setTextBoxToRight(true, 42, 14);

                releaseVSlider.setLabelPosition(LabeledSlider::LabelPosition::Top);
                releaseVSlider.setTextBoxToRight(true, 42, 14);

                int sH = 28;
                int sGap = 6;

                // Col 1: Attack & Decay
                attackVSlider.setBounds(col1.removeFromTop(sH));
                col1.removeFromTop(sGap);
                decayVSlider.setBounds(col1.removeFromTop(sH));

                // Col 2: Sustain & Release
                sustainVSlider.setBounds(col2.removeFromTop(sH));
                col2.removeFromTop(sGap);
                releaseVSlider.setBounds(col2.removeFromTop(sH));
            }
        }
        else // Voice & Pitch settings
        {
            voiceSectionLabel.setText("VOICE & PITCH", juce::dontSendNotification);
            voiceSectionLabel.setVisible(true);
            voiceModeLabel.setVisible(true);
            masterVoiceBox.setVisible(true);
            phaseModeLabel.setVisible(true);
            phaseModeBox.setVisible(true);
            pitchBendLabel.setVisible(true);
            pitchBendBox.setVisible(true);
            masterClipperToggle.setVisible(true);

            auto hdr = masterArea.removeFromTop(15);
            voiceSectionLabel.setBounds(hdr);
            masterArea.removeFromTop(isCompact ? 4 : 6);

            int vSpacing = isCompact ? 20 : 16;
            int vColW = (masterArea.getWidth() - vSpacing) / 2;
            auto col1 = masterArea.removeFromLeft(vColW);
            masterArea.removeFromLeft(vSpacing);
            auto col2 = masterArea;
            if (!isCompact)
                col2.removeFromRight(10);

            bool isPoly = (masterVoiceBox.getSelectedId() <= 1);

            if (isCompact)
            {
                // Compact: Values NEXT to settings text, 2 columns spanning full width
                int rowH = 18;
                int rowGap = 3;
                int labelW = 55;

                // Col 1 Row 1: Voice Mode
                auto r1 = col1.removeFromTop(rowH);
                voiceModeLabel.setBounds(r1.removeFromLeft(labelW));
                masterVoiceBox.setBounds(r1.reduced(0, 1));
                col1.removeFromTop(rowGap);

                // Col 1 Row 2: Glide (if Mono/Legato) or Soft Limiter (if Poly)
                if (!isPoly)
                {
                    masterGlideSlider.setVisible(true);
                    masterGlideSlider.setLabelPosition(LabeledSlider::LabelPosition::Left);
                    masterGlideSlider.setLabelWidth(labelW);
                    masterGlideSlider.setTextBoxToRight(false, 42, 14);
                    masterGlideSlider.setBounds(col1.removeFromTop(rowH));
                }
                else
                {
                    auto cr = col1.removeFromTop(rowH);
                    cr.removeFromLeft(labelW);
                    masterClipperToggle.setBounds(cr);
                }

                // Col 2 Row 1: Phase Mode
                auto r2 = col2.removeFromTop(rowH);
                phaseModeLabel.setBounds(r2.removeFromLeft(labelW));
                phaseModeBox.setBounds(r2.reduced(0, 1));
                col2.removeFromTop(rowGap);

                // Col 2 Row 2: Pitch Bend Range
                auto r3 = col2.removeFromTop(rowH);
                pitchBendLabel.setBounds(r3.removeFromLeft(labelW));
                pitchBendBox.setBounds(r3.reduced(0, 1));

                // If not poly, Soft Limiter sits in Col 2 Row 3
                if (!isPoly)
                {
                    col2.removeFromTop(rowGap);
                    auto cr = col2.removeFromTop(rowH);
                    cr.removeFromLeft(labelW);
                    masterClipperToggle.setBounds(cr);
                }
            }
            else
            {
                // Non compact: Two row, two column. Values to the RIGHT side of slider.
                // Col 1 Row 1: Voice Mode (Label on Top, Box below)
                voiceModeLabel.setBounds(col1.removeFromTop(12));
                masterVoiceBox.setBounds(col1.removeFromTop(20).reduced(0, 1));
                col1.removeFromTop(6);

                // Col 2 Row 1: Phase Mode (Label on Top, Box below)
                phaseModeLabel.setBounds(col2.removeFromTop(12));
                phaseModeBox.setBounds(col2.removeFromTop(20).reduced(0, 1));
                col2.removeFromTop(6);

                // Col 2 Row 2: Pitch Bend Range
                pitchBendLabel.setBounds(col2.removeFromTop(12));
                pitchBendBox.setBounds(col2.removeFromTop(20).reduced(0, 1));

                // Col 1 Row 2: Glide Slider (if not Poly, with value on right) or Soft Limiter
                if (!isPoly)
                {
                    masterGlideSlider.setVisible(true);
                    masterGlideSlider.setLabelPosition(LabeledSlider::LabelPosition::Top);
                    masterGlideSlider.setTextBoxToRight(true, 42, 14);
                    masterGlideSlider.setBounds(col1.removeFromTop(28));

                    col1.removeFromTop(4);
                    masterClipperToggle.setBounds(col1.removeFromTop(20));
                }
                else
                {
                    col1.removeFromTop(12);
                    masterClipperToggle.setBounds(col1.removeFromTop(20));
                }
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
            int masterHeight = 138;
            auto masterArea = mainWorkspace.removeFromBottom(masterHeight);
            mainWorkspace.removeFromBottom(8); // Spacing above master panel

            layoutMasterControls(masterArea, false);
        }

        // Partials Grid occupies the remaining workspace (pops up into the empty top space!)
        bool hasPartials = (numPartials > 0);
        partialsLabel.setVisible(false);
        partialsViewport.setVisible(hasPartials);

        if (hasPartials)
        {
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
        partialsLabel.setVisible(false);
        partialsViewport.setVisible(hasPartials);

        if (hasPartials)
        {
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
            int masterHeight = 138;
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

    auto propStr = property.toString();
    if (propStr.startsWith("CHORUS_") || propStr.startsWith("DELAY_") || propStr.startsWith("REVERB_"))
    {
        juce::MessageManager::callAsync([this]() {
            updateFxButtonsText();
        });
    }

    juce::String curPresetText = (factoryPresetBox.getSelectedId() > 0)
                                     ? factoryPresetBox.getText()
                                     : factoryPresetBox.getTextWhenNothingSelected();

    if (!isApplyingPreset && curPresetText != "Custom")
    {
        if (hasAnyParameterDivergedFromSnapshot())
        {
            juce::MessageManager::callAsync([this]() {
                factoryPresetBox.setTextWhenNothingSelected("Custom");
                if (factoryPresetBox.getSelectedId() != 0)
                    factoryPresetBox.setSelectedId(0, juce::dontSendNotification);
                factoryPresetBox.repaint();
            });
        }
    }
}

// ---------------------------------------------------------------------------
// Keyboard / MIDI Panic & Host Undo / Redo
// ---------------------------------------------------------------------------
bool WaveSlaveAudioProcessorEditor::keyPressed(const juce::KeyPress& key)
{
    // MIDI Panic: Escape key
    if (key.getKeyCode() == juce::KeyPress::escapeKey)
    {
        audioProcessor.allNotesOff();
        return true;
    }

    // Host Undo / Redo keyboard shortcuts (Ctrl+Z, Ctrl+Y, Ctrl+Shift+Z)
    if (key.getModifiers().isCtrlDown() || key.getModifiers().isCommandDown())
    {
        if (key.isKeyCode('Z') || key.isKeyCode('z'))
        {
            if (key.getModifiers().isShiftDown())
                audioProcessor.undoManager.redo();
            else
                audioProcessor.undoManager.undo();

            partialsContainer.refreshAllStrips();
            repaint();
            return true;
        }
        if (key.isKeyCode('Y') || key.isKeyCode('y'))
        {
            audioProcessor.undoManager.redo();
            partialsContainer.refreshAllStrips();
            repaint();
            return true;
        }
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

void WaveSlaveAudioProcessorEditor::saveUserPreset()
{
    auto userDocs = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    auto presetsDir = userDocs.getChildFile("WaveSlave").getChildFile("Presets");
    if (!presetsDir.exists())
        presetsDir.createDirectory();

    auto* aw = new juce::AlertWindow("Save User Preset", "Enter a name for your preset:", juce::AlertWindow::QuestionIcon);
    aw->addTextEditor("presetName", "My Patch", "Preset Name:");
    aw->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    aw->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    juce::Component::SafePointer<WaveSlaveAudioProcessorEditor> safeThis(this);
    aw->enterModalState(true, juce::ModalCallbackFunction::create([safeThis, aw, presetsDir](int result)
    {
        std::unique_ptr<juce::AlertWindow> windowDeleter(aw);
        if (safeThis == nullptr || result != 1) return;

        juce::String name = aw->getTextEditorContents("presetName").trim();
        if (name.isEmpty()) name = "My Patch";

        auto cleanName = juce::File::createLegalFileName(name);
        auto targetFile = presetsDir.getChildFile(cleanName).withFileExtension("waveslave");

        juce::XmlElement presetXml("WaveSlavePreset");
        presetXml.setAttribute("version", "1.1");
        presetXml.setAttribute("name", name);

        if (auto stateXml = safeThis->audioProcessor.apvts.copyState().createXml())
            presetXml.addChildElement(stateXml.release());

        safeThis->audioProcessor.serializeAnimationsToXml(presetXml);

        auto* wtXml = presetXml.createNewChildElement("CustomWavetables");
        for (int s = 0; s < 4; ++s)
        {
            auto* slotXml = wtXml->createNewChildElement("Slot");
            slotXml->setAttribute("index", s);
            juce::MemoryBlock mb(512 * sizeof(float));
            juce::MemoryOutputStream mos(mb, false);
            for (int i = 0; i < 512; ++i)
                mos.writeFloat(safeThis->audioProcessor.customWavetables[s][i].load(std::memory_order_relaxed));
            slotXml->setAttribute("samples", mb.toBase64Encoding());

            const auto& pts = safeThis->oscilloscope.getSlotSplinePoints(s);
            if (!pts.empty())
            {
                juce::String ptStr;
                for (const auto& pt : pts)
                    ptStr += juce::String(pt.x, 3) + "," + juce::String(pt.y, 3) + ";";
                slotXml->setAttribute("splinePoints", ptStr);
            }
        }

        if (presetXml.writeTo(targetFile))
        {
            safeThis->refreshPresetList();
            safeThis->factoryPresetBox.setTextWhenNothingSelected(name);
        }
    }));
}

void WaveSlaveAudioProcessorEditor::openPresetsFolder()
{
    auto userDocs = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    auto presetsDir = userDocs.getChildFile("WaveSlave").getChildFile("Presets");
    if (!presetsDir.exists())
        presetsDir.createDirectory();
    presetsDir.startAsProcess();
}

void WaveSlaveAudioProcessorEditor::refreshPresetList()
{
    factoryPresetBox.clear(juce::dontSendNotification);
    userPresetFiles.clear();

    // Factory presets 1..17
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
    factoryPresetBox.addItem("Alien Drone", 13);
    factoryPresetBox.addItem("Soft Pad", 14);
    factoryPresetBox.addItem("Seraphim", 15);
    factoryPresetBox.addItem("808 Drum Bass", 16);
    factoryPresetBox.addItem("Random", 17);

    // User presets from Documents/WaveSlave/Presets
    auto userDocs = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    auto presetsDir = userDocs.getChildFile("WaveSlave").getChildFile("Presets");
    if (presetsDir.exists())
    {
        auto files = presetsDir.findChildFiles(juce::File::findFiles, false, "*.waveslave;*.xml");
        if (!files.isEmpty())
        {
            factoryPresetBox.addSeparator();
            int id = 100;
            for (const auto& f : files)
            {
                userPresetFiles.push_back(f);
                factoryPresetBox.addItem(f.getFileNameWithoutExtension(), id++);
            }
        }
    }
}


