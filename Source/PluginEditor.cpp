#include "PluginProcessor.h"
#include "PluginEditor.h"

WaveSlaveAudioProcessorEditor::WaveSlaveAudioProcessorEditor (WaveSlaveAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p), oscilloscope(p), spectrograph(p)
{
    setLookAndFeel(&customLookAndFeel);
    setSize (900, 650);

    // Visualizers
    addAndMakeVisible(oscilloscope);
    addAndMakeVisible(spectrograph);

    // Title
    titleLabel.setText("WAVESLAVE", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(20.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xFFFFFFFF));
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(titleLabel);

    // Base Note selector
    baseNoteLabel.setText("Base Note:", juce::dontSendNotification);
    baseNoteLabel.setFont(juce::Font(11.0f));
    baseNoteLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.65f));
    baseNoteLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(baseNoteLabel);

    addAndMakeVisible(baseNoteBox);
    for (int note = 24; note <= 96; ++note)
    {
        float freq = (float)juce::MidiMessage::getMidiNoteInHertz(note);
        juce::String name = juce::MidiMessage::getMidiNoteName(note, true, true, 3);
        baseNoteBox.addItem(name + " (" + juce::String((int)std::round(freq)) + " Hz)", note - 23);
    }
    baseNoteAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(audioProcessor.apvts, "BASE_NOTE", baseNoteBox);
    baseNoteBox.onChange = [this]()
    {
        int note = (int)audioProcessor.apvts.getRawParameterValue("BASE_NOTE")->load();
        float f = (float)juce::MidiMessage::getMidiNoteInHertz(note);
        audioProcessor.lastPlayedFrequency.store(f, std::memory_order_relaxed);
        spectrograph.repaint();
        oscilloscope.repaint();
    };

    // Base Waveform selector
    baseWaveformLabel.setText("Wave:", juce::dontSendNotification);
    baseWaveformLabel.setFont(juce::Font(11.0f));
    baseWaveformLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.65f));
    baseWaveformLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(baseWaveformLabel);

    addAndMakeVisible(baseWaveformBox);
    baseWaveformBox.addItemList({ "Sine", "Triangle", "Saw", "Square", "Pulse", "Sharkfin", "Golden Spiral", "Noise", "Custom" }, 1);
    baseWaveformAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(audioProcessor.apvts, "BASE_WAVEFORM", baseWaveformBox);

    // Presets
    addAndMakeVisible(factoryPresetBox);
    factoryPresetBox.setTextWhenNothingSelected("Custom");
    factoryPresetBox.addItem("Reset", 1);
    factoryPresetBox.addItem("Saw Pluck", 2);
    factoryPresetBox.addItem("Soft Pad", 3);
    factoryPresetBox.addItem("Drawbar Organ", 4);
    factoryPresetBox.addItem("Glassy Keys", 5);
    factoryPresetBox.addItem("Thick Sub Bass", 6);
    factoryPresetBox.addItem("Alien Drone", 7);
    factoryPresetBox.addItem("Fifth Chord Lead", 8);
    factoryPresetBox.addItem("Fibonacci Pluck", 9);
    factoryPresetBox.addItem("Sharkfin Brass", 10);
    factoryPresetBox.addItem("Chiptune", 11);
    factoryPresetBox.addItem("Wobbling Square", 12);
    factoryPresetBox.addItem("Evolving Texture", 13);
    factoryPresetBox.addItem("Too Much Glue!", 14);
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
                    auto state = audioProcessor.apvts.copyState();
                    std::unique_ptr<juce::XmlElement> xml (state.createXml());
                    if (xml) xml->writeTo(result);
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
                    std::unique_ptr<juce::XmlElement> xmlState(juce::XmlDocument::parse(result));
                    if (xmlState && xmlState->hasTagName(audioProcessor.apvts.state.getType())) {
                        isApplyingPreset = true;
                        audioProcessor.apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
                        isApplyingPreset = false;
                        factoryPresetBox.setSelectedId(0, juce::dontSendNotification);
                    }
                }
            });
        }
        // Reset selection back to "File"
        fileMenuBox.setSelectedId(0, juce::dontSendNotification);
    };

    // Envelope section label
    envelopeLabel.setText("ENVELOPE", juce::dontSendNotification);
    envelopeLabel.setFont(juce::Font(11.0f));
    envelopeLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.5f));
    envelopeLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(envelopeLabel);

    // Partials section label
    partialsLabel.setText("HARMONICS", juce::dontSendNotification);
    partialsLabel.setFont(juce::Font(11.0f, juce::Font::bold));
    partialsLabel.setColour(juce::Label::textColourId, juce::Colour(0xFFFFFFFF));
    partialsLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(partialsLabel);

    // Global Master Gain slider
    addAndMakeVisible(masterGainSlider);
    masterGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "MASTER_GAIN", masterGainSlider.slider);

    addAndMakeVisible(partialsComboBox);
    partialsComboBox.addItem("No Partials", 1);
    for (int i = 1; i <= 16; ++i)
    {
        partialsComboBox.addItem(juce::String(i) + (i == 1 ? " Partial" : " Partials"), i + 1);
    }
    partialsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(audioProcessor.apvts, "NUM_PARTIALS", partialsComboBox);

    // Bulk edit controls
    auto setupBulkBtn = [this](juce::TextButton& btn, const juce::String& text, juce::Colour bgCol, juce::Colour txtCol)
    {
        btn.setButtonText(text);
        btn.setColour(juce::TextButton::buttonColourId, bgCol);
        btn.setColour(juce::TextButton::textColourOffId, txtCol);
        addAndMakeVisible(btn);
    };

    addAndMakeVisible(bulkEditBox);
    bulkEditBox.setTextWhenNothingSelected("Bulk Edit");
    bulkEditBox.addItem("Waveforms: All Sine", 1);
    bulkEditBox.addItem("Waveforms: All Triangle", 2);
    bulkEditBox.addItem("Waveforms: All Sawtooth", 3);
    bulkEditBox.addItem("Waveforms: All Square", 4);
    bulkEditBox.addItem("Waveforms: All Sharkfin", 6);
    bulkEditBox.addItem("Waveforms: All Golden Spiral", 7);
    bulkEditBox.addItem("Waveforms: All Custom", 8);
    bulkEditBox.addSeparator();
    bulkEditBox.addItem("Harmonics: Natural Series", 10);
    bulkEditBox.addItem("Harmonics: Odd Only", 11);
    bulkEditBox.addItem("Harmonics: Even Only", 12);
    bulkEditBox.addItem("Harmonics: Octaves", 13);
    bulkEditBox.addItem("Harmonics: Inharmonic/Bell", 14);
    bulkEditBox.addSeparator();
    bulkEditBox.addItem("Gain: 1/n Rolloff", 20);
    bulkEditBox.addItem("Gain: 1/n^2 Rolloff", 21);
    bulkEditBox.addItem("Gain: Flat (All 100%)", 22);
    bulkEditBox.addItem("Gain: Inverted", 23);
    bulkEditBox.addItem("Gain: Solo Fundamental", 24);
    bulkEditBox.addSeparator();
    bulkEditBox.addItem("Detune: Zero Detune", 30);
    bulkEditBox.addItem("Detune: Subtle Spread", 31);
    bulkEditBox.addItem("Detune: Wide Spread", 32);
    bulkEditBox.addSeparator();
    bulkEditBox.addItem("Pitch: Reset Semitones", 40);
    bulkEditBox.addSeparator();
    bulkEditBox.addItem("Mod Target: None", 50);
    bulkEditBox.addItem("Mod Target: Pitch (All)", 51);
    bulkEditBox.addItem("Mod Target: Level (All)", 52);
    bulkEditBox.addItem("Mod Depth: 50%", 53);
    bulkEditBox.addSeparator();
    bulkEditBox.addItem("Reset All Partials to Default", 99);

    bulkEditBox.onChange = [this]()
    {
        int result = bulkEditBox.getSelectedId();
        if (result == 0) return;

        if (result == 1) applyBulkWaveform(0);
        else if (result == 2) applyBulkWaveform(1);
        else if (result == 3) applyBulkWaveform(2);
        else if (result == 4) applyBulkWaveform(3);
        else if (result == 6) applyBulkWaveform(5); // Sharkfin
        else if (result == 7) applyBulkWaveform(6); // Golden Spiral
        else if (result == 8) applyBulkWaveform(8); // Custom
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
        else if (result == 40) applyBulkSemitones(0);
        else if (result == 50) applyBulkModTarget(0);
        else if (result == 51) applyBulkModTarget(1);
        else if (result == 52) applyBulkModTarget(2);
        else if (result == 53) applyBulkModDepth(0.5f);
        else if (result == 99) resetAllPartials();

        bulkEditBox.setSelectedId(0, juce::dontSendNotification);
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

    // Partials viewport (vertical scroll on right side)
    addAndMakeVisible(partialsViewport);
    partialsViewport.setViewedComponent(&partialsContainer, false);
    partialsViewport.setScrollBarsShown(true, false); // vertical scroll only

    // Two-way connection between spectrum pucks and partial strip cards
    spectrograph.onPartialSelected = [this](int idx)
    {
        partialsContainer.setSelectedPartial(idx, &partialsViewport);
    };

    partialsContainer.initPartials(audioProcessor.apvts, 16, [this](int idx)
    {
        spectrograph.setSelectedPartial(idx);
    });

    // Dynamic partials count change hook
    partialsComboBox.onChange = [this]()
    {
        int count = partialsComboBox.getSelectedId() - 1;
        if (count < 0) count = 0;
        setNumPartials(count);
    };

    int initialCount = (int)audioProcessor.apvts.getRawParameterValue("NUM_PARTIALS")->load();
    partialsContainer.setActiveCount(initialCount);
    partialsComboBox.setSelectedId(initialCount + 1, juce::dontSendNotification);
    if (initialCount == 0)
        partialsLabel.setText("HARMONICS (No active partials)", juce::dontSendNotification);
    else
        partialsLabel.setText("HARMONICS (Showing " + juce::String(initialCount) + " of 16 active partials)", juce::dontSendNotification);

    audioProcessor.apvts.state.addListener(this);
}

WaveSlaveAudioProcessorEditor::~WaveSlaveAudioProcessorEditor()
{
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

    // Visualizers & Master Left Area
    int numPartials = (int)audioProcessor.apvts.getRawParameterValue("NUM_PARTIALS")->load();
    int harmonicsWidth = (numPartials > 0) ? 246 : 0;
    
    float leftWidth = (numPartials > 0) ? (float)getWidth() - harmonicsWidth - 24.0f : (float)getWidth() - 20.0f;
    auto leftArea = juce::Rectangle<float>(10.0f, 38.0f, leftWidth, (float)getHeight() - 48.0f);
    g.setColour(juce::Colour(0xFF202020));
    g.fillRoundedRectangle(leftArea, 6.0f);
    g.setColour(juce::Colour(0xFF353535));
    g.drawRoundedRectangle(leftArea, 6.0f, 1.0f);

    // Right-Side Harmonics Pane
    if (numPartials > 0)
    {
        auto rightArea = juce::Rectangle<float>((float)getWidth() - harmonicsWidth - 10.0f, 38.0f, (float)harmonicsWidth, (float)getHeight() - 48.0f);
        g.setColour(juce::Colour(0xFF202020));
        g.fillRoundedRectangle(rightArea, 6.0f);
        g.setColour(juce::Colour(0xFF353535));
        g.drawRoundedRectangle(rightArea, 6.0f, 1.0f);
    }
}

void WaveSlaveAudioProcessorEditor::setNumPartials(int numPartials)
{
    if (auto* p = audioProcessor.apvts.getParameter("NUM_PARTIALS"))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1((float)numPartials));
        p->endChangeGesture();
    }
    partialsContainer.setActiveCount(numPartials);
    
    bool hasPartials = (numPartials > 0);
    partialsLabel.setVisible(hasPartials);
    partialsViewport.setVisible(hasPartials);
    
    if (hasPartials)
        partialsLabel.setText("HARMONICS (" + juce::String(numPartials) + " of 16)", juce::dontSendNotification);

    partialsContainer.setSize(partialsViewport.getWidth() - 14, PartialsContainer::stripCardHeight * numPartials);
    partialsContainer.resized();
    
    // Trigger repaint of the main window so the background frame disappears
    repaint();
    spectrograph.repaint();
    oscilloscope.repaint();
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

void WaveSlaveAudioProcessorEditor::resetAllPartials()
{
    for (int i = 1; i <= 16; ++i)
    {
        juce::String s = juce::String(i);
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
    }
}

void WaveSlaveAudioProcessorEditor::applyFactoryPreset(int index)
{
    isApplyingPreset = true;
    
    // Clear all semitones across all 16 partials first so previous offsets don't leak
    applyBulkSemitones(0);

    if (index == 1) // Reset
    {
        resetAllPartials();
        oscilloscope.clearWavetable();
        if (auto* p = audioProcessor.apvts.getParameter("ATTACK")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.01f)); p->endChangeGesture(); }
        if (auto* p = audioProcessor.apvts.getParameter("DECAY")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.3f)); p->endChangeGesture(); }
        if (auto* p = audioProcessor.apvts.getParameter("SUSTAIN")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.8f)); p->endChangeGesture(); }
        if (auto* p = audioProcessor.apvts.getParameter("RELEASE")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.5f)); p->endChangeGesture(); }
        if (auto* p = audioProcessor.apvts.getParameter("BASE_NOTE")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(60.0f)); p->endChangeGesture(); }
        if (auto* p = audioProcessor.apvts.getParameter("BASE_WAVEFORM")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.0f)); p->endChangeGesture(); }
        if (auto* p = audioProcessor.apvts.getParameter("NUM_PARTIALS")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(3.0f)); p->endChangeGesture(); }
    }
    else if (index == 2) // Saw Pluck
    {
        applyBulkWaveform(2); // Saw for all 16
        applyBulkHarmonicSeries(0); // Natural for all 16
        applyBulkGainTilt(0); // 1/n for all 16
        applyBulkDetune(1); // Subtle spread for all 16
        applyBulkModTarget(0); // None
        if (auto* p = audioProcessor.apvts.getParameter("ATTACK")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.005f)); p->endChangeGesture(); }
        if (auto* p = audioProcessor.apvts.getParameter("DECAY")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.4f)); p->endChangeGesture(); }
        if (auto* p = audioProcessor.apvts.getParameter("SUSTAIN")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.0f)); p->endChangeGesture(); }
        if (auto* p = audioProcessor.apvts.getParameter("RELEASE")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.3f)); p->endChangeGesture(); }
    }
    else if (index == 3) // Soft Pad
    {
        applyBulkWaveform(0); // Sine for all 16
        applyBulkHarmonicSeries(0); // Natural for all 16
        applyBulkGainTilt(0); // 1/n tilt for all 16
        applyBulkDetune(2); // Wide spread for all 16
        applyBulkModTarget(1); // Pitch Mod for lush chorusing
        applyBulkModDepth(0.02f);
        if (auto* p = audioProcessor.apvts.getParameter("ATTACK")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(1.5f)); p->endChangeGesture(); }
        if (auto* p = audioProcessor.apvts.getParameter("DECAY")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(2.0f)); p->endChangeGesture(); }
        if (auto* p = audioProcessor.apvts.getParameter("SUSTAIN")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.8f)); p->endChangeGesture(); }
        if (auto* p = audioProcessor.apvts.getParameter("RELEASE")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(2.5f)); p->endChangeGesture(); }
    }
    else if (index == 4) // Drawbar Organ
    {
        applyBulkWaveform(0); // Sine for all 16
        applyBulkHarmonicSeries(3); // Octaves
        applyBulkGainTilt(2); // Flat for all 16
        applyBulkDetune(0); // Zero
        applyBulkModTarget(2); // Level Mod for tremolo
        applyBulkModDepth(0.2f);
        for (int i = 1; i <= 16; ++i) {
            if (auto* p = audioProcessor.apvts.getParameter("MOD_RATE" + juce::String(i))) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(6.0f)); p->endChangeGesture(); }
        }
        if (auto* p = audioProcessor.apvts.getParameter("ATTACK")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.01f)); p->endChangeGesture(); }
        if (auto* p = audioProcessor.apvts.getParameter("DECAY")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.1f)); p->endChangeGesture(); }
        if (auto* p = audioProcessor.apvts.getParameter("SUSTAIN")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(1.0f)); p->endChangeGesture(); }
        if (auto* p = audioProcessor.apvts.getParameter("RELEASE")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.05f)); p->endChangeGesture(); }
    }
    else if (index == 5) // Glassy Keys
    {
        applyBulkWaveform(0); // Sine for all 16
        applyBulkHarmonicSeries(1); // Odd
        for (int i = 1; i <= 16; ++i)
        {
            if (auto* p = audioProcessor.apvts.getParameter("GAIN" + juce::String(i))) {
                float g = 1.0f / (1.0f + 0.35f * (i - 1));
                p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(g)); p->endChangeGesture();
            }
        }
        applyBulkDetune(1); // Subtle spread for all 16
        applyBulkModTarget(1);
        applyBulkModDepth(0.01f);
        if (auto* p = audioProcessor.apvts.getParameter("ATTACK")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.05f)); p->endChangeGesture(); }
        if (auto* p = audioProcessor.apvts.getParameter("DECAY")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.5f)); p->endChangeGesture(); }
        if (auto* p = audioProcessor.apvts.getParameter("SUSTAIN")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.2f)); p->endChangeGesture(); }
        if (auto* p = audioProcessor.apvts.getParameter("RELEASE")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.8f)); p->endChangeGesture(); }
    }
    else if (index == 6) // Thick Sub Bass
    {
        applyBulkWaveform(1); // Triangle for all 16
        applyBulkHarmonicSeries(3); // Octaves
        applyBulkGainTilt(0); // 1/n for all 16
        applyBulkDetune(0); // Zero
        applyBulkModTarget(0);
        if (auto* p = audioProcessor.apvts.getParameter("ATTACK")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.02f)); p->endChangeGesture(); }
        if (auto* p = audioProcessor.apvts.getParameter("DECAY")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.8f)); p->endChangeGesture(); }
        if (auto* p = audioProcessor.apvts.getParameter("SUSTAIN")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.5f)); p->endChangeGesture(); }
        if (auto* p = audioProcessor.apvts.getParameter("RELEASE")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.3f)); p->endChangeGesture(); }
    }
    else if (index == 7) // Alien Drone
    {
        applyBulkWaveform(2); // Saw for all 16
        applyBulkHarmonicSeries(4); // Bell/Inharmonic for all 16
        applyBulkGainTilt(2); // Flat for all 16
        applyBulkDetune(2); // Wide spread for all 16
        applyBulkModTarget(1);
        applyBulkModDepth(0.08f);
        for (int i = 1; i <= 16; ++i) {
            if (auto* p = audioProcessor.apvts.getParameter("MOD_RATE" + juce::String(i))) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.2f + i * 0.1f)); p->endChangeGesture(); }
            if (auto* p = audioProcessor.apvts.getParameter("MOD_PHASE" + juce::String(i))) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1((i * 45) % 360)); p->endChangeGesture(); }
        }
        if (auto* p = audioProcessor.apvts.getParameter("ATTACK")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(2.0f)); p->endChangeGesture(); }
        if (auto* p = audioProcessor.apvts.getParameter("DECAY")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(1.0f)); p->endChangeGesture(); }
        if (auto* p = audioProcessor.apvts.getParameter("SUSTAIN")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(1.0f)); p->endChangeGesture(); }
        if (auto* p = audioProcessor.apvts.getParameter("RELEASE")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(3.0f)); p->endChangeGesture(); }
    }
    else if (index == 8) // Fifth Chord Lead
    {
        applyBulkWaveform(3); // Square for all 16
        applyBulkHarmonicSeries(0); // Natural for all 16
        for (int i = 1; i <= 16; ++i)
        {
            if (auto* p = audioProcessor.apvts.getParameter("GAIN" + juce::String(i))) {
                float g = 1.0f / (1.0f + 0.3f * (i - 1));
                p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(g)); p->endChangeGesture();
            }
            if (auto* p = audioProcessor.apvts.getParameter("SEMITONE" + juce::String(i))) {
                float semi = (i % 2 == 0) ? 7.0f : 0.0f; // Perfect fifth on even harmonics
                p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(semi)); p->endChangeGesture();
            }
        }
        applyBulkDetune(1); // Subtle spread for all 16
        applyBulkModTarget(0);
        if (auto* p = audioProcessor.apvts.getParameter("ATTACK")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.01f)); p->endChangeGesture(); }
        if (auto* p = audioProcessor.apvts.getParameter("DECAY")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.3f)); p->endChangeGesture(); }
        if (auto* p = audioProcessor.apvts.getParameter("SUSTAIN")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.3f)); p->endChangeGesture(); }
        if (auto* p = audioProcessor.apvts.getParameter("RELEASE")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.4f)); p->endChangeGesture(); }
    }
    else if (index == 9) // Fibonacci Pluck
    {
        applyBulkWaveform(6); // Golden Spiral for all 16
        applyBulkHarmonicSeries(4); // Bell/Inharmonic for all 16
        applyBulkGainTilt(0); // 1/n for all 16
        applyBulkDetune(1); // Subtle spread for all 16
        applyBulkModTarget(2); // Level mod
        applyBulkModDepth(0.4f);
        for (int i = 1; i <= 16; ++i) {
            if (auto* p = audioProcessor.apvts.getParameter("MOD_RATE" + juce::String(i))) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(12.0f)); p->endChangeGesture(); }
            if (auto* p = audioProcessor.apvts.getParameter("MOD_WAVEFORM" + juce::String(i))) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(2.0f)); p->endChangeGesture(); } // Saw
        }
        if (auto* p = audioProcessor.apvts.getParameter("ATTACK")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.001f)); p->endChangeGesture(); }
        if (auto* p = audioProcessor.apvts.getParameter("DECAY")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.4f)); p->endChangeGesture(); }
        if (auto* p = audioProcessor.apvts.getParameter("SUSTAIN")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.0f)); p->endChangeGesture(); }
        if (auto* p = audioProcessor.apvts.getParameter("RELEASE")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.4f)); p->endChangeGesture(); }
    }
    else if (index == 10) // Sharkfin Brass
    {
        applyBulkWaveform(5); // Sharkfin for all 16
        applyBulkHarmonicSeries(0); // Natural for all 16
        applyBulkGainTilt(2); // Flat for all 16
        applyBulkDetune(2); // Wide spread for all 16
        applyBulkModTarget(0);
        if (auto* p = audioProcessor.apvts.getParameter("ATTACK")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.15f)); p->endChangeGesture(); }
        if (auto* p = audioProcessor.apvts.getParameter("DECAY")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.5f)); p->endChangeGesture(); }
        if (auto* p = audioProcessor.apvts.getParameter("SUSTAIN")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.8f)); p->endChangeGesture(); }
        if (auto* p = audioProcessor.apvts.getParameter("RELEASE")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.6f)); p->endChangeGesture(); }
    }
    else if (index == 11) // Lo-Fi Chiptune
    {
        applyBulkWaveform(4); // Pulse for all 16
        applyBulkHarmonicSeries(3); // Octaves
        applyBulkGainTilt(2); // Flat for all 16
        applyBulkDetune(0); // Zero for all 16
        applyBulkModTarget(1); // Pitch Mod for 8-bit vibrato
        applyBulkModDepth(0.04f);
        for (int i = 1; i <= 16; ++i) {
            if (auto* p = audioProcessor.apvts.getParameter("MOD_RATE" + juce::String(i))) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(12.0f)); p->endChangeGesture(); }
            if (auto* p = audioProcessor.apvts.getParameter("MOD_WAVEFORM" + juce::String(i))) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(3.0f)); p->endChangeGesture(); } // Square
        }
        if (auto* p = audioProcessor.apvts.getParameter("WAVEFORM4")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(7.0f)); p->endChangeGesture(); }
        if (auto* p = audioProcessor.apvts.getParameter("GAIN4")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.4f)); p->endChangeGesture(); }
        if (auto* p = audioProcessor.apvts.getParameter("WAVEFORM8")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(7.0f)); p->endChangeGesture(); }
        if (auto* p = audioProcessor.apvts.getParameter("GAIN8")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.3f)); p->endChangeGesture(); }
        if (auto* p = audioProcessor.apvts.getParameter("ATTACK")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.001f)); p->endChangeGesture(); }
        if (auto* p = audioProcessor.apvts.getParameter("DECAY")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.1f)); p->endChangeGesture(); }
        if (auto* p = audioProcessor.apvts.getParameter("SUSTAIN")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(1.0f)); p->endChangeGesture(); }
        if (auto* p = audioProcessor.apvts.getParameter("RELEASE")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.05f)); p->endChangeGesture(); }
    }
    else if (index == 12) // Wobbling Square
    {
        applyBulkWaveform(3); // Square
        applyBulkHarmonicSeries(1); // Odd
        applyBulkGainTilt(0); 
        applyBulkDetune(1);
        applyBulkModTarget(2); // Level
        applyBulkModDepth(0.8f);
        for (int i = 1; i <= 16; ++i) {
            if (auto* p = audioProcessor.apvts.getParameter("MOD_RATE" + juce::String(i))) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(1.5f)); p->endChangeGesture(); }
            if (auto* p = audioProcessor.apvts.getParameter("MOD_WAVEFORM" + juce::String(i))) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.0f)); p->endChangeGesture(); } // Sine
        }
        if (auto* p = audioProcessor.apvts.getParameter("ATTACK")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.1f)); p->endChangeGesture(); }
        if (auto* p = audioProcessor.apvts.getParameter("DECAY")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.5f)); p->endChangeGesture(); }
        if (auto* p = audioProcessor.apvts.getParameter("SUSTAIN")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(1.0f)); p->endChangeGesture(); }
        if (auto* p = audioProcessor.apvts.getParameter("RELEASE")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.3f)); p->endChangeGesture(); }
    }
    else if (index == 13) // Evolving Texture
    {
        applyBulkWaveform(0); // Sine
        applyBulkHarmonicSeries(4); // Inharmonic
        applyBulkGainTilt(2); // Flat
        applyBulkDetune(2); // Wide
        applyBulkModTarget(1); // Pitch
        applyBulkModDepth(0.05f);
        for (int i = 1; i <= 16; ++i) {
            if (auto* p = audioProcessor.apvts.getParameter("MOD_RATE" + juce::String(i))) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.1f * i)); p->endChangeGesture(); }
            if (auto* p = audioProcessor.apvts.getParameter("MOD_WAVEFORM" + juce::String(i))) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.0f)); p->endChangeGesture(); }
            if (auto* p = audioProcessor.apvts.getParameter("MOD_PHASE" + juce::String(i))) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1((i * 67) % 360)); p->endChangeGesture(); }
        }
        if (auto* p = audioProcessor.apvts.getParameter("ATTACK")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(3.0f)); p->endChangeGesture(); }
        if (auto* p = audioProcessor.apvts.getParameter("DECAY")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(2.0f)); p->endChangeGesture(); }
        if (auto* p = audioProcessor.apvts.getParameter("SUSTAIN")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(1.0f)); p->endChangeGesture(); }
        if (auto* p = audioProcessor.apvts.getParameter("RELEASE")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(4.0f)); p->endChangeGesture(); }
    }
    else if (index == 14) // Too Much Glue!
    {
        applyBulkWaveform(1); // Triangle
        applyBulkDetune(0);
        
        float ratios[16] = {1.0f, 1.75f, 4.0f, 8.0f, 16.0f, 32.0f, 32.0f, 32.0f, 32.0f, 32.0f, 32.0f, 32.0f, 32.0f, 32.0f, 32.0f, 32.0f};
        float gains[16] = {1.0f, 0.24f, 0.33f, 0.25f, 0.2f, 0.17f, 0.14f, 0.13f, 0.11f, 0.1f, 0.09f, 0.08f, 0.08f, 0.07f, 0.07f, 0.06f};
        float modRates[16] = {0.1f, 0.25f, 1.23f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        float modTargets[16] = {1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        float modDepths[16] = {1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

        for (int i = 1; i <= 16; ++i) {
            if (auto* p = audioProcessor.apvts.getParameter("RATIO" + juce::String(i))) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(ratios[i-1])); p->endChangeGesture(); }
            if (auto* p = audioProcessor.apvts.getParameter("GAIN" + juce::String(i))) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(gains[i-1])); p->endChangeGesture(); }
            if (auto* p = audioProcessor.apvts.getParameter("MOD_TARGET" + juce::String(i))) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(modTargets[i-1])); p->endChangeGesture(); }
            if (auto* p = audioProcessor.apvts.getParameter("MOD_RATE" + juce::String(i))) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(modRates[i-1])); p->endChangeGesture(); }
            if (auto* p = audioProcessor.apvts.getParameter("MOD_DEPTH" + juce::String(i))) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(modDepths[i-1])); p->endChangeGesture(); }
        }
        
        if (auto* p = audioProcessor.apvts.getParameter("ATTACK")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.02f)); p->endChangeGesture(); }
        if (auto* p = audioProcessor.apvts.getParameter("DECAY")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.8f)); p->endChangeGesture(); }
        if (auto* p = audioProcessor.apvts.getParameter("SUSTAIN")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.5f)); p->endChangeGesture(); }
        if (auto* p = audioProcessor.apvts.getParameter("RELEASE")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.3f)); p->endChangeGesture(); }
        if (auto* p = audioProcessor.apvts.getParameter("MASTER_GAIN")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(0.54f)); p->endChangeGesture(); }
        if (auto* p = audioProcessor.apvts.getParameter("NUM_PARTIALS")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(3.0f)); p->endChangeGesture(); }
    }

    spectrograph.repaint();
    oscilloscope.repaint();
    
    juce::Component::SafePointer<WaveSlaveAudioProcessorEditor> safeThis(this);
    juce::Timer::callAfterDelay(150, [safeThis]() {
        if (safeThis != nullptr)
            safeThis->isApplyingPreset = false;
    });
    
    if (index == 1) // Reset
    {
        factoryPresetBox.setTextWhenNothingSelected("Presets");
        factoryPresetBox.setSelectedId(0, juce::dontSendNotification);
    }
}

void WaveSlaveAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    // Top Header row across full width
    auto headerArea = area.removeFromTop(34).reduced(10, 3);
    titleLabel.setBounds(headerArea.removeFromLeft(140));

    // Base Note & Waveform selector controls
    baseNoteLabel.setBounds(headerArea.removeFromLeft(70));
    baseNoteBox.setBounds(headerArea.removeFromLeft(125).reduced(2, 2));

    baseWaveformLabel.setBounds(headerArea.removeFromLeft(46));
    baseWaveformBox.setBounds(headerArea.removeFromLeft(105).reduced(2, 2));

    // Presets, File Menu, Bulk Edit, and Partials dropdowns on right side of header
    fileMenuBox.setBounds(headerArea.removeFromRight(60).reduced(2, 2));
    factoryPresetBox.setBounds(headerArea.removeFromRight(90).reduced(2, 2));
    bulkEditBox.setBounds(headerArea.removeFromRight(110).reduced(2, 2));
    partialsComboBox.setBounds(headerArea.removeFromRight(110).reduced(2, 2));

    // Main workspace below header
    auto mainWorkspace = area.reduced(10);

    // Right-Side Harmonics Pane
    int numPartials = (int)audioProcessor.apvts.getRawParameterValue("NUM_PARTIALS")->load();
    int harmonicsWidth = (numPartials > 0) ? 246 : 0;
    auto rightPane = mainWorkspace.removeFromRight(harmonicsWidth);

    partialsLabel.setBounds(rightPane.removeFromTop(20));
    rightPane.removeFromTop(4);
    partialsViewport.setBounds(rightPane);
    partialsContainer.setSize(partialsViewport.getWidth() - 14, PartialsContainer::stripCardHeight * partialsContainer.getActiveCount());
    partialsContainer.resized();

    if (numPartials > 0)
        mainWorkspace.removeFromRight(10); // spacing between left controls and harmonics

    // Left/Center Area
    // Bottom: Oscilloscope (square) + Master controls (sliders) side-by-side
    int bottomRowHeight = 220; 
    auto bottomRow = mainWorkspace.removeFromBottom(bottomRowHeight);
    
    // Oscilloscope (square)
    oscilloscope.setBounds(bottomRow.removeFromLeft(bottomRowHeight).reduced(2));
    
    // Spacing
    bottomRow.removeFromLeft(16);
    
    // Master Controls
    auto masterArea = bottomRow;
    
    envelopeLabel.setBounds(masterArea.removeFromTop(18));
    masterArea.removeFromTop(4);
    
    int masterSliderH = masterArea.getHeight() / 5;
    masterGainSlider.setBounds(masterArea.removeFromTop(masterSliderH).reduced(0, 2));
    attackVSlider.setBounds(masterArea.removeFromTop(masterSliderH).reduced(0, 2));
    decayVSlider.setBounds(masterArea.removeFromTop(masterSliderH).reduced(0, 2));
    sustainVSlider.setBounds(masterArea.removeFromTop(masterSliderH).reduced(0, 2));
    releaseVSlider.setBounds(masterArea.removeFromTop(masterSliderH).reduced(0, 2));

    mainWorkspace.removeFromBottom(8);

    // Remaining top area: Spectrograph
    spectrograph.setBounds(mainWorkspace.reduced(2));
}

void WaveSlaveAudioProcessorEditor::valueTreePropertyChanged(juce::ValueTree& treeWhosePropertyHasChanged, const juce::Identifier& property)
{
    juce::ignoreUnused(treeWhosePropertyHasChanged);
    
    // Check if Base Waveform changed and overwrite custom table if not Custom
    if (property.toString() == "BASE_WAVEFORM")
    {
        int waveType = (int)audioProcessor.apvts.getRawParameterValue("BASE_WAVEFORM")->load();
        if (waveType >= 0 && waveType < 8)
        {
            audioProcessor.generateStandardWaveToCustomTable(waveType);
        }
    }

    if (!isApplyingPreset)
    {
        juce::MessageManager::callAsync([this]() {
            factoryPresetBox.setTextWhenNothingSelected("Custom");
            if (factoryPresetBox.getSelectedId() != 0)
                factoryPresetBox.setSelectedId(0, juce::dontSendNotification);
        });
    }
}
