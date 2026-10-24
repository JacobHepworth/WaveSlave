#include "Visualizer.h"

//==============================================================================
// Oscilloscope

Oscilloscope::Oscilloscope(WaveSlaveAudioProcessor& p) : processor(p)
{
    for (int s = 0; s < 4; ++s)
    {
        if (!processor.customSplinePoints[s].empty())
            slotSplinePoints[s] = processor.customSplinePoints[s];
    }

    displayBuffer.setSize(1, 512);
    displayBuffer.clear();
    partialDisplayBuffer.setSize(16, 512);
    partialDisplayBuffer.clear();

    overlayBtn.setClickingTogglesState(true);
    overlayBtn.setToggleState(false, juce::dontSendNotification);
    overlayBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF444444));
    overlayBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFFFFFFFF).withAlpha(0.25f));
    overlayBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white.withAlpha(0.5f));
    overlayBtn.setColour(juce::TextButton::textColourOnId, juce::Colour(0xFFFFFFFF));
    overlayBtn.onClick = [this]() {
        showOverlays = overlayBtn.getToggleState();
        repaint();
    };
    addAndMakeVisible(overlayBtn);

    polarBtn.setClickingTogglesState(true);
    polarBtn.setToggleState(false, juce::dontSendNotification);
    polarBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF444444));
    polarBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFFFFFFFF).withAlpha(0.25f));
    polarBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white.withAlpha(0.5f));
    polarBtn.setColour(juce::TextButton::textColourOnId, juce::Colour(0xFFFFFFFF));
    polarBtn.onClick = [this]() {
        setPolarMode(polarBtn.getToggleState());
    };
    addAndMakeVisible(polarBtn);

    freezeBtn.setClickingTogglesState(true);
    freezeBtn.setToggleState(false, juce::dontSendNotification);
    freezeBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF444444));
    freezeBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFF007ACC));
    freezeBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white.withAlpha(0.5f));
    freezeBtn.setColour(juce::TextButton::textColourOnId, juce::Colour(0xFFFFFFFF));
    freezeBtn.setTooltip("Freeze / Hold Oscilloscope waveform display");
    freezeBtn.onClick = [this]() {
        isFrozen = freezeBtn.getToggleState();
    };
    addAndMakeVisible(freezeBtn);

    drawBtn.setClickingTogglesState(true);
    drawBtn.setToggleState(false, juce::dontSendNotification);
    drawBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF444444));
    drawBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFFFFFFFF).withAlpha(0.25f));
    drawBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white.withAlpha(0.5f));
    drawBtn.setColour(juce::TextButton::textColourOnId, juce::Colour(0xFFFFFFFF));
    drawBtn.onClick = [this]() {
        isDrawMode = drawBtn.getToggleState();
        if (isDrawMode) { setPolarMode(false); polarBtn.setToggleState(false, juce::dontSendNotification); }
        bezierToggle.setVisible(isDrawMode);
        slotSelector.setVisible(isDrawMode);
        clearBtn.setVisible(isDrawMode);
        loadWavBtn.setVisible(isDrawMode);
        repaint();
    };
    addAndMakeVisible(drawBtn);

    // Initial default spline points for each slot
    slotSplinePoints[0] = { {0.0f, 0.0f}, {0.25f, 1.0f}, {0.5f, 0.0f}, {0.75f, -1.0f}, {1.0f, 0.0f} }; // Sine
    slotSplinePoints[1] = { {0.0f, 0.0f}, {0.25f, 1.0f}, {0.75f, -1.0f}, {1.0f, 0.0f} };               // Tri
    slotSplinePoints[2] = { {0.0f, 0.0f}, {0.49f, 1.0f}, {0.51f, -1.0f}, {1.0f, 0.0f} };               // Saw
    slotSplinePoints[3] = { {0.0f, 1.0f}, {0.49f, 1.0f}, {0.51f, -1.0f}, {1.0f, -1.0f} };              // Square

    slotSelector.setScrollWheelEnabled(false);
    slotSelector.addItem("Custom 1", 1);
    slotSelector.addItem("Custom 2", 2);
    slotSelector.addItem("Custom 3", 3);
    slotSelector.addItem("Custom 4", 4);
    slotSelector.setSelectedId(1, juce::dontSendNotification);
    slotSelector.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xFF222222));
    slotSelector.setColour(juce::ComboBox::textColourId, juce::Colours::white);
    slotSelector.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xFF444444));
    slotSelector.onChange = [this]() {
        setSlot(slotSelector.getSelectedId() - 1);
    };
    addChildComponent(slotSelector); // hidden until DRAW mode

    bezierToggle.setColour(juce::ToggleButton::textColourId, juce::Colours::white);
    bezierToggle.setToggleState(true, juce::dontSendNotification);
    bezierToggle.onClick = [this]() { repaint(); };
    addChildComponent(bezierToggle); // hidden until DRAW mode

    loadWavBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF333333));
    loadWavBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    loadWavBtn.setTooltip("Load single-cycle .wav file into current custom wavetable slot");
    loadWavBtn.onClick = [this]() {
        fileChooser = std::make_unique<juce::FileChooser>(
            "Load Single-Cycle Audio File",
            juce::File::getSpecialLocation(juce::File::userMusicDirectory),
            "*.wav;*.aif;*.aiff");

        auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
        fileChooser->launchAsync(flags, [this](const juce::FileChooser& fc) {
            auto file = fc.getResult();
            if (file.existsAsFile())
                loadWavFile(file);
        });
    };
    addChildComponent(loadWavBtn); // hidden until DRAW mode

    clearBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF333333));
    clearBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    clearBtn.onClick = [this]() {
        clearWavetable(currentSlot);
    };
    addChildComponent(clearBtn); // hidden until DRAW mode

    startTimerHz(30);
}

Oscilloscope::~Oscilloscope()
{
}

bool Oscilloscope::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& f : files)
    {
        if (f.endsWithIgnoreCase(".wav") || f.endsWithIgnoreCase(".aif") || f.endsWithIgnoreCase(".aiff"))
            return true;
    }
    return false;
}

void Oscilloscope::filesDropped(const juce::StringArray& files, int /*x*/, int /*y*/)
{
    for (const auto& f : files)
    {
        if (f.endsWithIgnoreCase(".wav") || f.endsWithIgnoreCase(".aif") || f.endsWithIgnoreCase(".aiff"))
        {
            loadWavFile(juce::File(f));
            break;
        }
    }
}

void Oscilloscope::loadWavFile(const juce::File& file)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(file));
    if (reader != nullptr && reader->lengthInSamples > 0)
    {
        int numSamples = (int)reader->lengthInSamples;
        juce::AudioBuffer<float> tempBuffer(1, numSamples);
        reader->read(&tempBuffer, 0, numSamples, 0, true, false);

        float maxAmp = 0.0001f;
        std::array<float, 512> newSamples {};

        for (int i = 0; i < 512; ++i)
        {
            float pos = ((float)i / 512.0f) * (float)numSamples;
            int idx0 = (int)pos % numSamples;
            int idx1 = (idx0 + 1) % numSamples;
            float frac = pos - (int)pos;

            float s0 = tempBuffer.getSample(0, idx0);
            float s1 = tempBuffer.getSample(0, idx1);
            float val = s0 + frac * (s1 - s0);

            newSamples[i] = val;
            maxAmp = juce::jmax(maxAmp, std::abs(val));
        }

        for (int i = 0; i < 512; ++i)
        {
            float normVal = newSamples[i] / maxAmp;
            processor.customWavetables[currentSlot][i].store(normVal, std::memory_order_relaxed);
        }

        slotSplinePoints[currentSlot].clear();
        processor.customSplinePoints[currentSlot].clear();
        repaint();
    }
}

void Oscilloscope::resized()
{
    int btnH = 18;
    int btnY = getHeight() - btnH - 4;
    int spacing = 4;
    int totalBtnW = getWidth() - 16;
    int btnW = (totalBtnW - 3 * spacing) / 4;
    drawBtn.setBounds(8, btnY, btnW, btnH);
    polarBtn.setBounds(8 + (btnW + spacing), btnY, btnW, btnH);
    freezeBtn.setBounds(8 + 2 * (btnW + spacing), btnY, btnW, btnH);
    overlayBtn.setBounds(8 + 3 * (btnW + spacing), btnY, totalBtnW - 3 * (btnW + spacing), btnH);

    // Top controls when DRAW mode is active (strict non-overlapping bounds)
    int topY = 4;
    int topH = 18;
    int slotW = 72;
    int wavW = 32;
    int clearW = 32;
    int smoothW = juce::jmax(50, getWidth() - 16 - slotW - wavW - clearW - 12);
    slotSelector.setBounds(8, topY, slotW, topH);
    bezierToggle.setBounds(8 + slotW + 4, topY, smoothW, topH);
    loadWavBtn.setBounds(8 + slotW + 4 + smoothW + 4, topY, wavW, topH);
    clearBtn.setBounds(8 + slotW + 4 + smoothW + 4 + wavW + 4, topY, clearW, topH);
}

void Oscilloscope::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Dark background with rounded corners
    g.setColour(juce::Colour(0xFF0A0A0A));
    g.fillRoundedRectangle(bounds, 8.0f);

    // Subtle border
    g.setColour(juce::Colour(0xFF333333));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 8.0f, 1.0f);

    if (!isDrawMode)
    {
        // Label at top
        g.setColour(juce::Colours::white.withAlpha(0.6f));
        g.setFont(11.0f);
        g.drawText(isPolar ? "POLAR OSCILLOSCOPE" : "OSCILLOSCOPE", 12, 6, 200, 14, juce::Justification::centredLeft, false);
    }

    // Inner graph area reserved between top title/controls and bottom buttons
    auto graphArea = bounds.reduced(12.0f);
    graphArea.removeFromTop(isDrawMode ? 14.0f : 12.0f);
    graphArea.removeFromBottom(16.0f);

    auto centerX = graphArea.getCentreX();
    auto centerY = graphArea.getCentreY();
    auto width = graphArea.getWidth();
    auto height = graphArea.getHeight();
    auto left = graphArea.getX();
    auto top = graphArea.getY();

    float maxR = std::min(width, height) * 0.42f;
    float baseR = maxR * 0.65f;

    auto getPoint = [&](float s, int i, int n, float maxMag = 0.38f) -> juce::Point<float>
    {
        if (isPolar)
        {
            float phase = (float)i / (float)n;
            float angle = juce::MathConstants<float>::twoPi * phase;
            // Linear deviation from base radius, clamped safely within maxR
            float r = juce::jlimit(baseR * 0.15f, maxR * 1.12f, baseR + s * (maxR - baseR) * (maxMag / 0.38f));
            return { centerX + r * std::sin(angle), centerY - r * std::cos(angle) };
        }
        return { juce::jmap((float)i, 0.0f, (float)(n - 1), left, left + width), centerY - s * height * maxMag };
    };

    if (isPolar)
    {
        // Concentric guide rings
        g.setColour(juce::Colour(0xFF252525));
        g.drawEllipse(centerX - maxR, centerY - maxR, maxR * 2.0f, maxR * 2.0f, 1.0f); // Peak ring
        g.setColour(juce::Colour(0xFF404040));
        g.drawEllipse(centerX - baseR, centerY - baseR, baseR * 2.0f, baseR * 2.0f, 1.2f); // Baseline (0V)
        g.setColour(juce::Colour(0xFF151515));
        float innerR = baseR * 0.35f;
        g.drawEllipse(centerX - innerR, centerY - innerR, innerR * 2.0f, innerR * 2.0f, 1.0f); // Trough ring

        // Radial cross axes
        g.drawLine(centerX, centerY - maxR - 4.0f, centerX, centerY + maxR + 4.0f, 1.0f);
        g.drawLine(centerX - maxR - 4.0f, centerY, centerX + maxR + 4.0f, centerY, 1.0f);

        // Polar Notation
        g.setColour(juce::Colours::white.withAlpha(0.35f));
        g.setFont(8.5f);
        // Degree / phase angle notation
        g.drawText(juce::String(juce::CharPointer_UTF8("0\xc2\xb0")), (int)centerX - 15, (int)(centerY - maxR - 13.0f), 30, 10, juce::Justification::centred);
        g.drawText(juce::String(juce::CharPointer_UTF8("90\xc2\xb0")), (int)(centerX + maxR + 3.0f), (int)centerY - 6, 26, 12, juce::Justification::centredLeft);
        g.drawText(juce::String(juce::CharPointer_UTF8("180\xc2\xb0")), (int)centerX - 15, (int)(centerY + maxR + 2.0f), 30, 10, juce::Justification::centred);
        g.drawText(juce::String(juce::CharPointer_UTF8("270\xc2\xb0")), (int)(centerX - maxR - 28.0f), (int)centerY - 6, 26, 12, juce::Justification::centredRight);

        // Amplitude ring labels
        g.setColour(juce::Colours::white.withAlpha(0.4f));
        g.setFont(7.5f);
        g.drawText("0", (int)(centerX + 3.0f), (int)(centerY - baseR - 4.0f), 16, 8, juce::Justification::topLeft);
        g.drawText("+1", (int)(centerX + 3.0f), (int)(centerY - maxR - 2.0f), 16, 8, juce::Justification::topLeft);
    }
    else
    {
        // Grid lines
        g.setColour(juce::Colour(0xFF1A1A1A));
        for (int i = 1; i < 4; ++i)
        {
            float yLine = top + height * (float)i / 4.0f;
            g.drawHorizontalLine((int)yLine, left, left + width);
        }

        // Center line
        g.setColour(juce::Colour(0xFF303030));
        g.drawHorizontalLine((int)centerY, left, left + width);
    }

    int numActive = (int)processor.apvts.getRawParameterValue("NUM_PARTIALS")->load();
    numActive = juce::jlimit(0, 16, numActive);

    // Overlapping partial waveforms
    if (showOverlays)
    {
        auto numSamples = partialDisplayBuffer.getNumSamples();
        for (int ch = 0; ch < numActive && ch < partialDisplayBuffer.getNumChannels(); ++ch)
        {
            auto* pRead = partialDisplayBuffer.getReadPointer(ch);
            float maxMag = 0.0f;
            for (int s = 0; s < numSamples; s += 8)
                maxMag = juce::jmax(maxMag, std::abs(pRead[s]));

            if (maxMag > 0.0005f)
            {
                juce::Path partPath;
                partPath.startNewSubPath(getPoint(pRead[0], 0, numSamples));
                for (int s = 1; s < numSamples; ++s)
                {
                    partPath.lineTo(getPoint(pRead[s], s, numSamples));
                }

                auto col = getPartialColour(ch);
                g.setColour(col.withAlpha(0.18f));
                g.strokePath(partPath, juce::PathStrokeType(3.0f));
                g.setColour(col.withAlpha(0.70f));
                g.strokePath(partPath, juce::PathStrokeType(1.2f));
            }
        }
    }

    // Composite waveform path
    auto numSamples = displayBuffer.getNumSamples();
    auto* readPointer = displayBuffer.getReadPointer(0);

    juce::Path p;
    if (numSamples > 0)
    {
        p.startNewSubPath(getPoint(readPointer[0], 0, numSamples));

        for (int i = 1; i < numSamples; ++i)
        {
            p.lineTo(getPoint(readPointer[i], i, numSamples));
        }
    }

    // Glow (thick pass)
    g.setColour(juce::Colours::white.withAlpha(0.2f));
    g.strokePath(p, juce::PathStrokeType(6.0f));

    // Main line (Composite)
    g.setColour(juce::Colour(0xFFFFFFFF));
    g.strokePath(p, juce::PathStrokeType(1.6f));

    if (isDrawMode)
    {
        g.setColour(juce::Colours::white.withAlpha(0.6f));
        g.drawRect(graphArea, 1.5f);

        if (bezierToggle.getToggleState())
        {
            float maxVisualH = height * 0.38f;
            float maxRadius = std::min(width, height) * 0.45f;
            auto& points = slotSplinePoints[currentSlot];
            for (const auto& pt : points)
            {
                float px, py;
                if (isPolar)
                {
                    float angle = pt.x * juce::MathConstants<float>::twoPi;
                    float r = maxRadius * (0.5f + 0.5f * pt.y);
                    px = centerX + r * std::sin(angle);
                    py = centerY - r * std::cos(angle);
                }
                else
                {
                    px = left + pt.x * width;
                    py = centerY - pt.y * maxVisualH;
                }
                g.setColour(juce::Colour(0xFFFF9100));
                g.fillEllipse(px - 3.5f, py - 3.5f, 7.0f, 7.0f);
                g.setColour(juce::Colours::white.withAlpha(0.9f));
                g.drawEllipse(px - 3.5f, py - 3.5f, 7.0f, 7.0f, 1.0f);
            }
        }
    }
}

void Oscilloscope::setSlot(int slotIndex)
{
    currentSlot = juce::jlimit(0, 3, slotIndex);
    if (slotSelector.getSelectedId() != currentSlot + 1)
        slotSelector.setSelectedId(currentSlot + 1, juce::dontSendNotification);
    repaint();
}

void Oscilloscope::clearWavetable(int slot)
{
    int s = (slot >= 0 && slot < 4) ? slot : currentSlot;
    if (s >= 0 && s < 4)
    {
        slotSplinePoints[s].clear();
        processor.customSplinePoints[s].clear();
        for (int i = 0; i < 512; ++i)
        {
            processor.customWavetables[s][i].store(0.0f, std::memory_order_relaxed);
        }
    }
    repaint();
}

void Oscilloscope::updateWavetableFromSpline()
{
    auto& points = slotSplinePoints[currentSlot];
    if (points.empty()) return;

    processor.customSplinePoints[currentSlot] = points;

    std::sort(points.begin(), points.end(), [](const juce::Point<float>& a, const juce::Point<float>& b) {
        return a.x < b.x;
    });

    for (int i = 0; i < 512; ++i)
    {
        float x = (float)i / 511.0f;
        
        if (x <= points.front().x) {
            processor.customWavetables[currentSlot][i].store(points.front().y, std::memory_order_relaxed);
            continue;
        }
        if (x >= points.back().x) {
            processor.customWavetables[currentSlot][i].store(points.back().y, std::memory_order_relaxed);
            continue;
        }

        int seg = 0;
        for (; seg < (int)points.size() - 1; ++seg) {
            if (x >= points[seg].x && x <= points[seg + 1].x) break;
        }

        float x0 = points[seg].x;
        float x1 = points[seg + 1].x;
        float t = (x1 > x0) ? (x - x0) / (x1 - x0) : 0.0f;

        float y0 = (seg - 1 >= 0) ? points[seg - 1].y : points[seg].y;
        float y1 = points[seg].y;
        float y2 = points[seg + 1].y;
        float y3 = (seg + 2 < (int)points.size()) ? points[seg + 2].y : points[seg + 1].y;

        float t2 = t * t;
        float t3 = t2 * t;
        float y = 0.5f * (
            (2.0f * y1) +
            (-y0 + y2) * t +
            (2.0f * y0 - 5.0f * y1 + 4.0f * y2 - y3) * t2 +
            (-y0 + 3.0f * y1 - 3.0f * y2 + y3) * t3
        );

        y = juce::jlimit(-1.0f, 1.0f, y);
        processor.customWavetables[currentSlot][i].store(y, std::memory_order_relaxed);
    }
}

void Oscilloscope::drawOnWavetable(const juce::MouseEvent& e, bool isDrag, bool isRelease)
{
    if (!isDrawMode) return;

    auto bounds = getLocalBounds().toFloat();
    auto graphArea = bounds.reduced(12.0f);
    graphArea.removeFromTop(14.0f);
    graphArea.removeFromBottom(16.0f);

    float left = graphArea.getX();
    float width = graphArea.getWidth();
    float height = graphArea.getHeight();
    float centerX = graphArea.getCentreX();
    float centerY = graphArea.getCentreY();
    float maxVisualH = height * 0.38f;
    float maxRadius = std::min(width, height) * 0.45f;
    
    float normX = 0.0f;
    float normY = 0.0f;

    if (isPolar)
    {
        float dx = e.position.x - centerX;
        float dy = centerY - e.position.y;
        float angle = std::atan2(dx, dy);
        if (angle < 0) angle += juce::MathConstants<float>::twoPi;
        normX = angle / juce::MathConstants<float>::twoPi;

        float r = std::hypot(dx, dy);
        normY = (r / maxRadius - 0.5f) * 2.0f;
    }
    else
    {
        normX = (e.position.x - left) / width;
        normY = -(e.position.y - centerY) / maxVisualH;
    }
    
    normX = juce::jlimit(0.0f, 1.0f, normX);
    normY = juce::jlimit(-1.0f, 1.0f, normY);

    auto& splinePoints = slotSplinePoints[currentSlot];

    if (bezierToggle.getToggleState())
    {
        float hitThresholdPx = 15.0f; // 15 pixels

        auto getPixelDist = [&](juce::Point<float> pt) {
            if (isPolar) {
                float angle = pt.x * juce::MathConstants<float>::twoPi;
                float r = maxRadius * (0.5f + 0.5f * pt.y);
                float px = centerX + r * std::sin(angle);
                float py = centerY - r * std::cos(angle);
                return std::hypot(px - e.position.x, py - e.position.y);
            } else {
                float px = left + pt.x * width;
                float py = centerY - pt.y * maxVisualH;
                return std::hypot(px - e.position.x, py - e.position.y);
            }
        };

        if (!isDrag && !isRelease) // mouseDown
        {
            if (e.mods.isRightButtonDown())
            {
                // Right click: delete
                for (int i = 0; i < (int)splinePoints.size(); ++i) {
                    if (getPixelDist(splinePoints[i]) < hitThresholdPx) {
                        splinePoints.erase(splinePoints.begin() + i);
                        updateWavetableFromSpline();
                        return;
                    }
                }
            }
            else // Left click
            {
                // Try to select point
                draggedPointIndex = -1;
                for (int i = 0; i < (int)splinePoints.size(); ++i) {
                    if (getPixelDist(splinePoints[i]) < hitThresholdPx) {
                        draggedPointIndex = i;
                        break;
                    }
                }
                
                // If clicked on empty space, add a point
                if (draggedPointIndex == -1) {
                    splinePoints.push_back({normX, normY});
                    updateWavetableFromSpline();
                    
                    // Select the newly added point for immediate dragging
                    draggedPointIndex = -1;
                    std::sort(splinePoints.begin(), splinePoints.end(), [](const juce::Point<float>& a, const juce::Point<float>& b) { return a.x < b.x; });
                    for (int i = 0; i < (int)splinePoints.size(); ++i) {
                        if (splinePoints[i].x == normX && splinePoints[i].y == normY) {
                            draggedPointIndex = i;
                            break;
                        }
                    }
                }
            }
        }
        else if (isDrag) // mouseDrag
        {
            if (draggedPointIndex != -1 && draggedPointIndex < (int)splinePoints.size()) {
                splinePoints[draggedPointIndex] = {normX, normY};
                updateWavetableFromSpline();
            }
        }
        else if (isRelease) // mouseUp
        {
            draggedPointIndex = -1;
            std::sort(splinePoints.begin(), splinePoints.end(), [](const juce::Point<float>& a, const juce::Point<float>& b) { return a.x < b.x; });
            updateWavetableFromSpline();
        }
        return;
    }

    // Freedraw Mode
    int idx = juce::jlimit(0, 511, (int)(normX * 511.0f));
    processor.customWavetables[currentSlot][idx].store(normY, std::memory_order_relaxed);
    
    static int lastIdx = -1;
    if (isDrag && lastIdx != -1)
    {
        int start = std::min(lastIdx, idx);
        int end = std::max(lastIdx, idx);
        if (end - start > 1 && end - start < 200)
        {
            float vStart = processor.customWavetables[currentSlot][start].load(std::memory_order_relaxed);
            float vEnd = processor.customWavetables[currentSlot][end].load(std::memory_order_relaxed);
            for (int i = start + 1; i < end; ++i)
            {
                float interp = (float)(i - start) / (float)(end - start);
                processor.customWavetables[currentSlot][i].store(vStart + interp * (vEnd - vStart), std::memory_order_relaxed);
            }
        }
    }
    lastIdx = isRelease ? -1 : idx;
}

void Oscilloscope::mouseDown(const juce::MouseEvent& e) { drawOnWavetable(e, false, false); }
void Oscilloscope::mouseDrag(const juce::MouseEvent& e) { drawOnWavetable(e, true, false); }
void Oscilloscope::mouseUp(const juce::MouseEvent& e)   { drawOnWavetable(e, false, true); }

void Oscilloscope::timerCallback()
{
    if (!isShowing() || isFrozen)
        return;

    if (isDrawMode)
    {
        for (int i = 0; i < 512; ++i)
        {
            float val = processor.customWavetables[currentSlot][i].load(std::memory_order_relaxed);
            displayBuffer.setSample(0, i, val);
            for (int ch = 0; ch < 16; ++ch)
                partialDisplayBuffer.setSample(ch, i, 0.0f); // hide partials in draw mode
        }
        repaint();
        return;
    }

    int writePos = processor.ringBufferWritePos.load(std::memory_order_acquire);
    
    // Default fallback start position
    int startPos = (writePos - 2048 + processor.ringBufferSize) % processor.ringBufferSize;

    // Search backwards for a positive zero crossing to stabilize the waveform
    for (int i = 0; i < 2048; ++i)
    {
        int curr = (writePos - 512 - i + processor.ringBufferSize * 2) % processor.ringBufferSize;
        int prev = (curr - 1 + processor.ringBufferSize) % processor.ringBufferSize;

        if (processor.ringBuffer[prev] < 0.0f && processor.ringBuffer[curr] >= 0.0f)
        {
            startPos = curr;
            break;
        }
    }
    
    float sampleRate = processor.getSampleRate();
    if (sampleRate <= 0.0f) sampleRate = 44100.0f;
    float baseFreq = processor.lastPlayedFrequency.load(std::memory_order_relaxed);
    if (baseFreq <= 0.0f) baseFreq = 440.0f;
    
    // Calculate the number of samples in one fundamental period
    float periodSamples = sampleRate / baseFreq;
    
    // We want to display exactly 1 cycle in polar mode, or 2 cycles in linear mode
    float targetCycles = isPolar ? 1.0f : 2.0f;
    
    // Clamp period to avoid reading too far or too little
    periodSamples = juce::jlimit(10.0f, (float)processor.ringBufferSize / targetCycles - 1.0f, periodSamples);
    
    float stepSize = (periodSamples * targetCycles) / 512.0f;
    
    for (int i = 0; i < 512; ++i)
    {
        float exactPos = startPos + i * stepSize;
        int idx1 = (int)exactPos;
        int idx2 = idx1 + 1;
        float frac = exactPos - (float)idx1;
        
        idx1 %= processor.ringBufferSize;
        idx2 %= processor.ringBufferSize;
        if (idx1 < 0) idx1 += processor.ringBufferSize;
        if (idx2 < 0) idx2 += processor.ringBufferSize;
        
        float v1 = processor.ringBuffer[idx1];
        float v2 = processor.ringBuffer[idx2];
        displayBuffer.setSample(0, i, v1 + frac * (v2 - v1));
        
        for (int ch = 0; ch < 16; ++ch)
        {
            float p1 = processor.partialRingBuffer[ch][idx1];
            float p2 = processor.partialRingBuffer[ch][idx2];
            partialDisplayBuffer.setSample(ch, i, p1 + frac * (p2 - p1));
        }
    }

    repaint();
}

//==============================================================================
// Spectrograph

Spectrograph::Spectrograph(WaveSlaveAudioProcessor& p)
    : processor(p),
      forwardFFT(fftOrder),
      window(fftSize, juce::dsp::WindowingFunction<float>::hann)
{
    std::fill(std::begin(fftData), std::end(fftData), 0.0f);
    std::fill(std::begin(scopeData), std::end(scopeData), 0.0f);

    for (int i = 0; i < 16; ++i)
    {
        smoothedModRatio[i] = (float)(i + 1);
        smoothedModGain[i] = (i < 4) ? (1.0f / (float)(i + 1)) : 0.0f;
        smoothedModSemi[i] = 0.0f;
        smoothedModDetune[i] = 0.0f;
        smoothedModPan[i] = 0.0f;
        editorLfoPhases[i] = 0.0f;
        previewAnimElapsed[i] = 0.0f;
        previewAnimPhases[i] = 0.0f;
    }
    lastTimerMs = juce::Time::getMillisecondCounter();

    showNodesBtn.setClickingTogglesState(true);
    showNodesBtn.setToggleState(true, juce::dontSendNotification);
    showNodesBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF444444));
    showNodesBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFFFFFFFF).withAlpha(0.25f));
    showNodesBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white.withAlpha(0.5f));
    showNodesBtn.setColour(juce::TextButton::textColourOnId, juce::Colour(0xFFFFFFFF));
    showNodesBtn.onClick = [this]() {
        bool show = showNodesBtn.getToggleState();
        setReadOnly(!show);
        resized();
        repaint();
    };
    addAndMakeVisible(showNodesBtn);

    slideBtn.setClickingTogglesState(false);
    slideBtn.setTooltip("Sliding Spectrum View: Note-tracking harmonic ratio scale");
    slideBtn.onClick = [this]() { setSpectrumScaleMode(SpectrumScaleMode::Sliding); };
    addAndMakeVisible(slideBtn);

    fullBtn.setClickingTogglesState(false);
    fullBtn.setTooltip("Full Spectrum View: Fixed 20 Hz - 20,000 Hz logarithmic scale");
    fullBtn.onClick = [this]() { setSpectrumScaleMode(SpectrumScaleMode::Full); };
    addAndMakeVisible(fullBtn);

    setSpectrumScaleMode(SpectrumScaleMode::Sliding);

    lockBtn.setClickingTogglesState(true);
    lockBtn.setToggleState(false, juce::dontSendNotification);
    lockBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF333333));
    lockBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFF444444));
    lockBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white.withAlpha(0.6f));
    lockBtn.setColour(juce::TextButton::textColourOnId, juce::Colour(0xFFFFFFFF));
    lockBtn.setTooltip("Lock Nodes: Freeze node movement from live audio/modulation while allowing keyframe editing");
    lockBtn.onClick = [this]() {
        setNodesLocked(lockBtn.getToggleState());
    };
    addAndMakeVisible(lockBtn);

    freezeBtn.setClickingTogglesState(true);
    freezeBtn.setToggleState(false, juce::dontSendNotification);
    freezeBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF333333));
    freezeBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFF007ACC));
    freezeBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white.withAlpha(0.6f));
    freezeBtn.setColour(juce::TextButton::textColourOnId, juce::Colour(0xFFFFFFFF));
    freezeBtn.setTooltip("Freeze / Hold Visualizer frame");
    freezeBtn.onClick = [this]() {
        isFrozen = freezeBtn.getToggleState();
    };
    addAndMakeVisible(freezeBtn);

    buildWaterfallLut();

    // --- Interactive Keyframe Timeline Controls ---
    animEnableBtn.setClickingTogglesState(true);
    animEnableBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF252525));
    animEnableBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFF444444));
    animEnableBtn.setColour(juce::TextButton::textColourOnId, juce::Colour(0xFFFFFFFF));
    animEnableBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xFF777777));
    animEnableBtn.setTooltip("Enable / Disable Keyframe Animation for Selected Partial");
    animEnableBtn.onClick = [this]() {
        if (isSyncingTimelineControls || selectedPartial < 0) return;
        juce::String s = juce::String(selectedPartial + 1);
        bool turnOn = animEnableBtn.getToggleState();
        if (turnOn)
            processor.ensureInitialKeyframes(selectedPartial);
        if (auto* p = processor.apvts.getParameter("ANIM_MODE" + s))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost(p->convertTo0to1(turnOn ? 1.0f : 0.0f)); // Default to Note Length when turned on
            p->endChangeGesture();
        }
        syncTimelineControlsToSelectedPartial();
        if (onAnimationChanged) onAnimationChanged();
        repaint();
        repaintTimeline();
    };
    addAndMakeVisible(animEnableBtn);

    addKfBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF282828));
    addKfBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    addKfBtn.setTooltip("Insert or Update Keyframe at Current Playhead Time");
    addKfBtn.onClick = [this]() {
        if (selectedPartial < 0) return;
        juce::String s = juce::String(selectedPartial + 1);
        PartialAnimationData track = processor.getPartialAnimation(selectedPartial);
        PartialKeyframe baseKf = processor.getPartialBaseSettings(selectedPartial);
        int curve = 0;
        if (auto* cp = processor.apvts.getRawParameterValue("ANIM_CURVE" + s))
            curve = (int)cp->load();

        if (track.numKeyframes == 0 && scrubTimeNorm > 0.015f)
        {
            baseKf.time = 0.0f;
            processor.addOrUpdateKeyframe(selectedPartial, baseKf, 0.01f);
            track = processor.getPartialAnimation(selectedPartial);
        }

        const PartialKeyframe& fallbackKf = (track.numKeyframes > 0) ? track.keyframes[0] : baseKf;
        PartialKeyframe evalKf = (track.numKeyframes > 0) ? track.evaluate(scrubTimeNorm, curve, fallbackKf) : baseKf;
        evalKf.time = scrubTimeNorm;
        selectedKeyframeIndex = processor.addOrUpdateKeyframe(selectedPartial, evalKf, 0.012f);
        PartialAnimationData updated = processor.getPartialAnimation(selectedPartial);
        if (selectedKeyframeIndex >= 0 && selectedKeyframeIndex < updated.numKeyframes)
            scrubTimeNorm = updated.keyframes[selectedKeyframeIndex].time;

        // Ensure animation mode is enabled (defaulting to Note Length) when user explicitly clicks +KF
        if (auto* mp = processor.apvts.getParameter("ANIM_MODE" + s))
        {
            if (mp->convertFrom0to1(mp->getValue()) < 0.5f)
            {
                mp->beginChangeGesture();
                mp->setValueNotifyingHost(mp->convertTo0to1(1.0f)); // Note Length
                mp->endChangeGesture();
                syncTimelineControlsToSelectedPartial();
                if (onAnimationChanged) onAnimationChanged();
            }
        }
        syncSlidersToPlayhead(selectedPartial);
        repaint();
        repaintTimeline();
    };
    addAndMakeVisible(addKfBtn);

    delKfBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF282828));
    delKfBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white.withAlpha(0.85f));
    delKfBtn.setTooltip("Delete Selected Keyframe (or right-click diamond on timeline)");
    delKfBtn.onClick = [this]() {
        if (selectedPartial < 0) return;
        PartialAnimationData track = processor.getPartialAnimation(selectedPartial);
        int targetIdx = selectedKeyframeIndex;
        if (targetIdx < 0 || targetIdx >= track.numKeyframes)
        {
            for (int k = 0; k < track.numKeyframes; ++k)
            {
                if (std::abs(track.keyframes[k].time - scrubTimeNorm) < 0.03f)
                {
                    targetIdx = k;
                    break;
                }
            }
        }
        if (targetIdx >= 0 && targetIdx < track.numKeyframes)
        {
            processor.removeKeyframe(selectedPartial, targetIdx);
            selectedKeyframeIndex = -1;
            repaint();
            repaintTimeline();
        }
    };
    addAndMakeVisible(delKfBtn);

    clearKfBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF282828));
    clearKfBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    clearKfBtn.setTooltip("Clear All Keyframes for Selected Partial");
    clearKfBtn.onClick = [this]() {
        if (selectedPartial < 0) return;
        processor.clearKeyframes(selectedPartial);
        selectedKeyframeIndex = -1;
        repaint();
        repaintTimeline();
    };
    addAndMakeVisible(clearKfBtn);

    timelineLockBtn.setClickingTogglesState(true);
    timelineLockBtn.setToggleState(false, juce::dontSendNotification);
    timelineLockBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF282828));
    timelineLockBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFF444444));
    timelineLockBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white.withAlpha(0.6f));
    timelineLockBtn.setColour(juce::TextButton::textColourOnId, juce::Colour(0xFFFFFFFF));
    timelineLockBtn.setTooltip("Lock Nodes: Freeze node movement from live audio/modulation while allowing keyframe editing");
    timelineLockBtn.onClick = [this]() {
        setNodesLocked(timelineLockBtn.getToggleState());
    };
    addAndMakeVisible(timelineLockBtn);

    timelinePartialBox.setScrollWheelEnabled(false);
    timelinePartialBox.setTooltip("Select Active Partial");
    updateTimelinePartialBoxItems();
    timelinePartialBox.onChange = [this]() {
        if (isSyncingTimelineControls) return;
        int p = timelinePartialBox.getSelectedId() - 1;
        if (p >= 0 && p < 16)
        {
            setSelectedPartial(p);
            if (onPartialSelected) onPartialSelected(p);
        }
    };
    addAndMakeVisible(timelinePartialBox);

    timelinePrevBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF222222));
    timelinePrevBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    timelinePrevBtn.setTooltip("Previous Partial");
    timelinePrevBtn.onClick = [this]() {
        int numActive = 16;
        if (auto* np = processor.apvts.getRawParameterValue("NUM_PARTIALS"))
            numActive = juce::jlimit(1, 16, (int)np->load());
        int prev = (selectedPartial <= 0) ? (numActive - 1) : (selectedPartial - 1);
        setSelectedPartial(prev);
        if (onPartialSelected) onPartialSelected(prev);
    };
    addAndMakeVisible(timelinePrevBtn);

    timelineNextBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF222222));
    timelineNextBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    timelineNextBtn.setTooltip("Next Partial");
    timelineNextBtn.onClick = [this]() {
        int numActive = 16;
        if (auto* np = processor.apvts.getRawParameterValue("NUM_PARTIALS"))
            numActive = juce::jlimit(1, 16, (int)np->load());
        int next = (selectedPartial < 0 || selectedPartial >= numActive - 1) ? 0 : (selectedPartial + 1);
        setSelectedPartial(next);
        if (onPartialSelected) onPartialSelected(next);
    };
    addAndMakeVisible(timelineNextBtn);

    animModeBox.setScrollWheelEnabled(false);
    animModeBox.addItemList({ "Off", "Note Length", "Time: One-Shot", "Time: Loop", "Time: Ping-Pong" }, 1);
    animModeBox.setTooltip("Keyframe Mode: Note Length (relative to played note ADSR) or Time-Based (uses Duration)");
    animModeBox.onChange = [this]() {
        if (isSyncingTimelineControls || selectedPartial < 0) return;
        int mode = animModeBox.getSelectedId() - 1;
        if (mode < 0) return;
        if (mode > 0)
            processor.ensureInitialKeyframes(selectedPartial);
        juce::String s = juce::String(selectedPartial + 1);
        if (auto* p = processor.apvts.getParameter("ANIM_MODE" + s))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost(p->convertTo0to1((float)mode));
            p->endChangeGesture();
        }
        syncTimelineControlsToSelectedPartial();
        if (onAnimationChanged) onAnimationChanged();
        repaint();
        repaintTimeline();
    };
    addAndMakeVisible(animModeBox);

    animCurveBox.setScrollWheelEnabled(false);
    animCurveBox.addItemList({ "Smooth", "Linear", "Step" }, 1);
    animCurveBox.setTooltip("Keyframe Interpolation Curve Type");
    animCurveBox.onChange = [this]() {
        if (isSyncingTimelineControls || selectedPartial < 0) return;
        int curve = animCurveBox.getSelectedId() - 1;
        if (curve < 0) return;
        juce::String s = juce::String(selectedPartial + 1);
        if (auto* p = processor.apvts.getParameter("ANIM_CURVE" + s))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost(p->convertTo0to1((float)curve));
            p->endChangeGesture();
        }
        if (onAnimationChanged) onAnimationChanged();
        repaint();
        repaintTimeline();
    };
    addAndMakeVisible(animCurveBox);

    animDurSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    animDurSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 42, 18);
    animDurSlider.setRange(0.05, 16.0, 0.01);
    animDurSlider.setSkewFactor(0.4);
    animDurSlider.setTextValueSuffix("s");
    animDurSlider.setTooltip("Keyframe Animation Cycle Duration (seconds, used in Time modes)");
    animDurSlider.onValueChange = [this]() {
        if (isSyncingTimelineControls || selectedPartial < 0) return;
        juce::String s = juce::String(selectedPartial + 1);
        if (auto* p = processor.apvts.getParameter("ANIM_DURATION" + s))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost(p->convertTo0to1((float)animDurSlider.getValue()));
            p->endChangeGesture();
        }
        repaint();
        repaintTimeline();
    };
    addAndMakeVisible(animDurSlider);

    syncTimelineControlsToSelectedPartial();
    startTimerHz(60);
}

Spectrograph::~Spectrograph()
{
}

void Spectrograph::repaintTimeline()
{
    if (timelineHost != nullptr && timelineHost->isVisible())
        timelineHost->repaint();
}

void Spectrograph::setSpectrumScaleMode(SpectrumScaleMode mode)
{
    if (scaleMode != mode)
    {
        scaleMode = mode;
        if (isWaterfallMode)
        {
            waterfallImage = juce::Image();
            waterfallHistory.clear();
            waterfallLineY = 0;
            lastSeenTriggerCounter = -1;
            for (auto& dot : waterfallDots)
                dot.hasValidPos = false;
            dotAnimAlpha = 0.0f;
            isDotAnimRunning = false;
        }
    }

    bool isFull = (scaleMode == SpectrumScaleMode::Full);
    slideBtn.setToggleState(!isFull, juce::dontSendNotification);
    slideBtn.setColour(juce::TextButton::buttonColourId, !isFull ? juce::Colour(0xFF444444) : juce::Colour(0xFF252525));
    slideBtn.setColour(juce::TextButton::textColourOffId, !isFull ? juce::Colour(0xFFFFFFFF) : juce::Colour(0xFF777777));
    slideBtn.setColour(juce::TextButton::textColourOnId, juce::Colour(0xFFFFFFFF));

    fullBtn.setToggleState(isFull, juce::dontSendNotification);
    fullBtn.setColour(juce::TextButton::buttonColourId, isFull ? juce::Colour(0xFF444444) : juce::Colour(0xFF252525));
    fullBtn.setColour(juce::TextButton::textColourOffId, isFull ? juce::Colour(0xFFFFFFFF) : juce::Colour(0xFF777777));
    fullBtn.setColour(juce::TextButton::textColourOnId, juce::Colour(0xFFFFFFFF));

    repaint();
}

void Spectrograph::setNodesLocked(bool locked)
{
    isLocked = locked;
    lockBtn.setToggleState(locked, juce::dontSendNotification);
    lockBtn.setButtonText(locked ? "LOCKED" : "LOCK");
    timelineLockBtn.setToggleState(locked, juce::dontSendNotification);
    timelineLockBtn.setButtonText(locked ? "LOCKED" : "LOCK");
    repaint();
    repaintTimeline();
}

void Spectrograph::setTimelineComponent(juce::Component* comp)
{
    timelineHost = comp;
    juce::Component* parent = (comp != nullptr) ? comp : this;
    parent->addAndMakeVisible(timelinePrevBtn);
    parent->addAndMakeVisible(timelinePartialBox);
    parent->addAndMakeVisible(timelineNextBtn);
    parent->addAndMakeVisible(animEnableBtn);
    parent->addAndMakeVisible(addKfBtn);
    parent->addAndMakeVisible(delKfBtn);
    parent->addAndMakeVisible(clearKfBtn);
    parent->addAndMakeVisible(timelineLockBtn);
    parent->addAndMakeVisible(animModeBox);
    parent->addAndMakeVisible(animCurveBox);
    parent->addAndMakeVisible(animDurSlider);
    if (comp != nullptr)
        layoutKeyframeTimeline(comp->getLocalBounds());
}

void Spectrograph::setTimelineOpen(bool open)
{
    isTimelineOpen = open;
    if (timelineHost != nullptr)
    {
        timelineHost->setVisible(open);
        if (open)
            layoutKeyframeTimeline(timelineHost->getLocalBounds());
    }
    resized();
    repaint();
    repaintTimeline();
}

float Spectrograph::getEffectiveTimelineInfo(int partialIdx, int& outMode, float& outAtk, float& outDec, float& outRel) const
{
    partialIdx = juce::jlimit(0, 15, partialIdx);
    juce::String s = juce::String(partialIdx + 1);

    outMode = 0;
    if (auto* p = processor.apvts.getRawParameterValue("ANIM_MODE" + s))
        outMode = (int)p->load();

    bool usePartialAdsr = false;
    if (auto* pae = processor.apvts.getRawParameterValue("P_ADSR_ENABLED" + s))
        usePartialAdsr = (pae->load() > 0.5f);

    if (usePartialAdsr)
    {
        outAtk = processor.apvts.getRawParameterValue("P_ATTACK" + s)  ? processor.apvts.getRawParameterValue("P_ATTACK" + s)->load()  : 0.01f;
        outDec = processor.apvts.getRawParameterValue("P_DECAY" + s)   ? processor.apvts.getRawParameterValue("P_DECAY" + s)->load()   : 0.30f;
        outRel = processor.apvts.getRawParameterValue("P_RELEASE" + s) ? processor.apvts.getRawParameterValue("P_RELEASE" + s)->load() : 0.50f;
    }
    else
    {
        outAtk = processor.apvts.getRawParameterValue("ATTACK")  ? processor.apvts.getRawParameterValue("ATTACK")->load()  : 0.01f;
        outDec = processor.apvts.getRawParameterValue("DECAY")   ? processor.apvts.getRawParameterValue("DECAY")->load()   : 0.30f;
        outRel = processor.apvts.getRawParameterValue("RELEASE") ? processor.apvts.getRawParameterValue("RELEASE")->load() : 0.50f;
    }

    outAtk = juce::jmax(0.001f, outAtk);
    outDec = juce::jmax(0.001f, outDec);
    outRel = juce::jmax(0.001f, outRel);

    if (outMode <= 1) // Off or Note Length (relative to played note ADSR)
        return outAtk + outDec + outRel;

    float dur = 2.0f;
    if (auto* dp = processor.apvts.getRawParameterValue("ANIM_DURATION" + s))
        dur = juce::jmax(0.05f, dp->load());

    if (outMode == 4) // Time: Ping-Pong (round-trip is 2x Duration)
        return dur * 2.0f;

    return dur;
}

void Spectrograph::setSelectedPartial(int index)
{
    if (index >= 0)
        index = juce::jlimit(0, 15, index);
    else
        index = -1;

    if (selectedPartial != index)
    {
        selectedPartial = index;
        selectedKeyframeIndex = -1;
    }
    syncTimelineControlsToSelectedPartial();
    repaint();
    repaintTimeline();
}

void Spectrograph::updateTimelinePartialBoxItems()
{
    int numActive = 16;
    if (auto* np = processor.apvts.getRawParameterValue("NUM_PARTIALS"))
        numActive = juce::jlimit(1, 16, (int)np->load());

    int prevSelected = timelinePartialBox.getSelectedId();
    timelinePartialBox.clear(juce::dontSendNotification);
    for (int i = 0; i < numActive; ++i)
    {
        timelinePartialBox.addItem("H" + juce::String(i + 1), i + 1);
    }
    int newSel = juce::jlimit(1, numActive, prevSelected > 0 ? prevSelected : (selectedPartial + 1));
    timelinePartialBox.setSelectedId(newSel, juce::dontSendNotification);
}

void Spectrograph::syncTimelineControlsToSelectedPartial()
{
    isSyncingTimelineControls = true;
    updateTimelinePartialBoxItems();

    if (selectedPartial < 0)
    {
        timelinePartialBox.setSelectedId(0, juce::dontSendNotification);
        timelinePartialBox.setText("None", juce::dontSendNotification);
        timelinePartialBox.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xFF333333));
        timelinePartialBox.setColour(juce::ComboBox::arrowColourId, juce::Colour(0xFF777777));
        animEnableBtn.setEnabled(false);
        animEnableBtn.setToggleState(false, juce::dontSendNotification);
        animEnableBtn.setButtonText("OFF");
        animModeBox.setEnabled(false);
        animModeBox.setSelectedId(0, juce::dontSendNotification);
        animCurveBox.setEnabled(false);
        animCurveBox.setSelectedId(0, juce::dontSendNotification);
        animDurSlider.setEnabled(false);
        addKfBtn.setEnabled(false);
        delKfBtn.setEnabled(false);
        clearKfBtn.setEnabled(false);
        isSyncingTimelineControls = false;
        if (timelineHost != nullptr)
            layoutKeyframeTimeline(timelineHost->getLocalBounds());
        resized();
        repaintTimeline();
        return;
    }

    animEnableBtn.setEnabled(true);
    animModeBox.setEnabled(true);
    animCurveBox.setEnabled(true);
    animDurSlider.setEnabled(true);
    addKfBtn.setEnabled(true);
    delKfBtn.setEnabled(true);
    clearKfBtn.setEnabled(true);

    timelinePartialBox.setSelectedId(selectedPartial + 1, juce::dontSendNotification);
    auto col = getPartialColour(selectedPartial);
    timelinePartialBox.setColour(juce::ComboBox::outlineColourId, col);
    timelinePartialBox.setColour(juce::ComboBox::arrowColourId, col);

    juce::String s = juce::String(selectedPartial + 1);

    int mode = 0;
    if (auto* p = processor.apvts.getRawParameterValue("ANIM_MODE" + s))
        mode = (int)p->load();

    int curve = 0;
    if (auto* p = processor.apvts.getRawParameterValue("ANIM_CURVE" + s))
        curve = (int)p->load();

    float dur = 2.0f;
    if (auto* p = processor.apvts.getRawParameterValue("ANIM_DURATION" + s))
        dur = p->load();

    bool on = (mode > 0);
    animEnableBtn.setToggleState(on, juce::dontSendNotification);
    animEnableBtn.setButtonText(on ? "ON" : "OFF");
    animModeBox.setSelectedId(mode + 1, juce::dontSendNotification);
    animCurveBox.setSelectedId(curve + 1, juce::dontSendNotification);
    animDurSlider.setValue(dur, juce::dontSendNotification);

    isSyncingTimelineControls = false;
    if (timelineHost != nullptr)
        layoutKeyframeTimeline(timelineHost->getLocalBounds());
    resized();
    repaintTimeline();
}

void Spectrograph::syncSlidersToPlayhead(int partialIdx)
{
    partialIdx = juce::jlimit(0, 15, partialIdx);
    juce::String s = juce::String(partialIdx + 1);
    int mode = 0;
    if (auto* p = processor.apvts.getRawParameterValue("ANIM_MODE" + s))
        mode = (int)p->load();
    if (mode <= 0)
        return;

    PartialAnimationData track = processor.getPartialAnimation(partialIdx);
    if (track.numKeyframes <= 0)
        return;

    int curve = 0;
    if (auto* p = processor.apvts.getRawParameterValue("ANIM_CURVE" + s))
        curve = (int)p->load();

    PartialKeyframe evalKf = track.evaluate(scrubTimeNorm, curve, track.keyframes[0]);
    if (auto* rp = processor.apvts.getParameter("RATIO" + s)) {
        rp->beginChangeGesture();
        rp->setValueNotifyingHost(rp->convertTo0to1(evalKf.ratio));
        rp->endChangeGesture();
    }
    if (auto* gp = processor.apvts.getParameter("GAIN" + s)) {
        gp->beginChangeGesture();
        gp->setValueNotifyingHost(gp->convertTo0to1(evalKf.gain));
        gp->endChangeGesture();
    }
    if (auto* sp = processor.apvts.getParameter("SEMITONE" + s)) {
        sp->beginChangeGesture();
        sp->setValueNotifyingHost(sp->convertTo0to1(evalKf.semitone));
        sp->endChangeGesture();
    }
    if (auto* dp = processor.apvts.getParameter("DETUNE" + s)) {
        dp->beginChangeGesture();
        dp->setValueNotifyingHost(dp->convertTo0to1(evalKf.detune));
        dp->endChangeGesture();
    }
    if (auto* pp = processor.apvts.getParameter("PAN" + s)) {
        pp->beginChangeGesture();
        pp->setValueNotifyingHost(pp->convertTo0to1(evalKf.pan));
        pp->endChangeGesture();
    }
}

void Spectrograph::recordKeyframeFromSliders(int partialIndex)
{
    partialIndex = juce::jlimit(0, 15, partialIndex);
    juce::String s = juce::String(partialIndex + 1);
    int mode = 0;
    int curve = 0;
    if (auto* p = processor.apvts.getRawParameterValue("ANIM_MODE" + s))  mode  = (int)p->load();
    if (auto* p = processor.apvts.getRawParameterValue("ANIM_CURVE" + s)) curve = (int)p->load();

    PartialKeyframe kf = processor.getPartialBaseSettings(partialIndex);
    PartialAnimationData track = processor.getPartialAnimation(partialIndex);
    if (mode > 0)
    {
        if (isTimelineOpen)
        {
            kf.time = scrubTimeNorm;
            selectedKeyframeIndex = processor.addOrUpdateKeyframe(partialIndex, kf, 0.015f);
            PartialAnimationData updated = processor.getPartialAnimation(partialIndex);
            if (selectedKeyframeIndex >= 0 && selectedKeyframeIndex < updated.numKeyframes)
                scrubTimeNorm = updated.keyframes[selectedKeyframeIndex].time;
        }
        else if (track.numKeyframes > 0)
        {
            // Shift the keyframe curve by the slider delta so the node stays in sync without recording unwanted keyframes
            PartialKeyframe prevEval = track.evaluate(scrubTimeNorm, curve, track.keyframes[0]);
            float dRatio  = kf.ratio    - prevEval.ratio;
            float dGain   = kf.gain     - prevEval.gain;
            float dSemi   = kf.semitone - prevEval.semitone;
            float dPan    = kf.pan      - prevEval.pan;
            for (int k = 0; k < track.numKeyframes; ++k)
            {
                track.keyframes[k].ratio    = juce::jlimit(0.01f, 32.0f,  track.keyframes[k].ratio    + dRatio);
                track.keyframes[k].gain     = juce::jlimit(0.0f,  1.0f,   track.keyframes[k].gain     + dGain);
                track.keyframes[k].semitone = juce::jlimit(-24.0f, 24.0f, track.keyframes[k].semitone + dSemi);
                track.keyframes[k].detune   = 0.0f;
                track.keyframes[k].pan      = juce::jlimit(-1.0f,  1.0f,   track.keyframes[k].pan      + dPan);
            }
            processor.setPartialAnimation(partialIndex, track);
        }
    }

    float effRatio = juce::jlimit(0.01f, 32.0f, kf.ratio * std::pow(2.0f, kf.semitone / 12.0f));
    smoothedModRatio[partialIndex]  = effRatio;
    smoothedModGain[partialIndex]   = kf.gain;
    smoothedModSemi[partialIndex]   = kf.semitone;
    smoothedModDetune[partialIndex] = 0.0f;
    smoothedModPan[partialIndex]    = kf.pan;
    repaint();
    repaintTimeline();
}

juce::Rectangle<float> Spectrograph::getGraphPlotBounds() const
{
    auto inner = getLocalBounds().toFloat().reduced(12.0f);
    float left  = inner.getX();
    float top   = inner.getY() + 24.0f;
    float width = inner.getWidth();
    float height = juce::jmax(40.0f, inner.getHeight() - 52.0f);
    return { left, top, width, height };
}

juce::Rectangle<float> Spectrograph::getTimelineTrackBounds() const
{
    if (timelineHost != nullptr)
    {
        auto tlArea = timelineHost->getLocalBounds().toFloat();
        return { tlArea.getX() + 6.0f, tlArea.getY() + 27.0f, juce::jmax(40.0f, tlArea.getWidth() - 12.0f), juce::jmax(24.0f, tlArea.getHeight() - 31.0f) };
    }
    auto inner = getLocalBounds().toFloat().reduced(12.0f);
    float timelineTop = inner.getBottom() - 64.0f;
    return { inner.getX() + 6.0f, timelineTop + 27.0f, juce::jmax(40.0f, inner.getWidth() - 12.0f), 36.0f };
}

void Spectrograph::layoutKeyframeTimeline(juce::Rectangle<int> bounds)
{
    bool showTl = isTimelineOpen;
    int selMode = 0;
    if (selectedPartial >= 0)
    {
        if (auto* p = processor.apvts.getRawParameterValue("ANIM_MODE" + juce::String(selectedPartial + 1)))
            selMode = (int)p->load();
    }
    bool showDurSlider = showTl && (selMode >= 2); // Only show time Duration slider in Time-based modes

    timelinePrevBtn.setVisible(showTl);
    timelinePartialBox.setVisible(showTl);
    timelineNextBtn.setVisible(showTl);
    animEnableBtn.setVisible(showTl);
    addKfBtn.setVisible(showTl);
    delKfBtn.setVisible(showTl);
    clearKfBtn.setVisible(showTl);
    timelineLockBtn.setVisible(showTl);
    animModeBox.setVisible(showTl);
    animCurveBox.setVisible(showTl);
    animDurSlider.setVisible(showDurSlider);

    if (!showTl)
        return;

    int tlY = bounds.getY() + 3;
    int rowH = 22;

    // Top Right of Timeline Pane: ON/OFF Button and Mode/Curve/Duration dropdowns
    int rightEdge = bounds.getRight() - 6;

    // Standard ON/OFF toggle at top right
    int enableBtnW = 38;
    animEnableBtn.setBounds(rightEdge - enableBtnW, tlY, enableBtnW, rowH);
    rightEdge -= (enableBtnW + 8);

    int modeW = 104;
    animModeBox.setBounds(rightEdge - modeW, tlY, modeW, rowH);
    rightEdge -= (modeW + 6);

    int curveW = 72;
    animCurveBox.setBounds(rightEdge - curveW, tlY, curveW, rowH);
    rightEdge -= (curveW + 6);

    if (showDurSlider)
    {
        int durW = juce::jlimit(65, 100, rightEdge - (int)(bounds.getX() + 320));
        if (durW > 50)
        {
            animDurSlider.setBounds(rightEdge - durW, tlY, durW, rowH);
            rightEdge -= (durW + 6);
        }
    }

    // Top Left of Timeline Pane: Title, Partial selector (< H# >), Keyframe add/del/clear buttons, and Lock button
    int curX = bounds.getX() + 66; // Space for "TIMELINE" title

    timelinePrevBtn.setBounds(curX, tlY, 18, rowH);
    curX += 20;
    timelinePartialBox.setBounds(curX, tlY, 50, rowH);
    curX += 52;
    timelineNextBtn.setBounds(curX, tlY, 18, rowH);
    curX += 24;

    addKfBtn.setBounds(curX, tlY, 32, rowH);   curX += 35;
    delKfBtn.setBounds(curX, tlY, 32, rowH);   curX += 35;
    clearKfBtn.setBounds(curX, tlY, 32, rowH); curX += 38;

    int lockW = 46;
    timelineLockBtn.setBounds(curX, tlY, lockW, rowH);
}

float Spectrograph::getEffectiveBaseFreq() const
{
    if (isLocked)
    {
        if (auto* bn = processor.apvts.getRawParameterValue("BASE_NOTE"))
        {
            int note = juce::jlimit(24, 96, (int)bn->load());
            return (float)juce::MidiMessage::getMidiNoteInHertz(note);
        }
        return 261.625565f; // C4
    }
    float baseFreq = processor.lastPlayedFrequency.load(std::memory_order_relaxed);
    if (baseFreq <= 0.0f)
    {
        if (auto* bn = processor.apvts.getRawParameterValue("BASE_NOTE"))
        {
            int note = juce::jlimit(24, 96, (int)bn->load());
            return (float)juce::MidiMessage::getMidiNoteInHertz(note);
        }
        return 261.625565f;
    }
    return baseFreq;
}

void Spectrograph::resized()
{
    if (timelineHost != nullptr)
        layoutKeyframeTimeline(timelineHost->getLocalBounds());

    auto inner = getLocalBounds().toFloat().reduced(12.0f);
    int imgW = juce::jmax(1, (int)inner.getWidth());
    int imgH = juce::jmax(1, (int)(inner.getHeight() - 52.0f));
    if (waterfallImage.getWidth() != imgW || waterfallImage.getHeight() != imgH)
    {
        waterfallImage = juce::Image(juce::Image::RGB, imgW, imgH, true);
        waterfallImage.clear(waterfallImage.getBounds(), waterfallColour(0.0f));
        waterfallHistory.assign((size_t)(imgH * scopeSize), 0.0f);
        waterfallLineY = 0;
    }

    showNodesBtn.setVisible(true);
    freezeBtn.setVisible(true);
    lockBtn.setVisible(true);
    slideBtn.setVisible(true);
    fullBtn.setVisible(true);

    // Position buttons at the top right of the graph
    int right = getWidth() - 10;
    int topY = 4;
    int btnH = 20;

    int showNodesW = 52;
    int freezeW    = 52;
    int lockW      = 48;
    int fullW      = 44;
    int slideW     = 52;

    showNodesBtn.setBounds(right - showNodesW, topY, showNodesW, btnH);
    right -= (showNodesW + 4);

    freezeBtn.setBounds(right - freezeW, topY, freezeW, btnH);
    right -= (freezeW + 4);

    lockBtn.setBounds(right - lockW, topY, lockW, btnH);
    right -= (lockW + 8);

    fullBtn.setBounds(right - fullW, topY, fullW, btnH);
    right -= (fullW + 3);

    slideBtn.setBounds(right - slideW, topY, slideW, btnH);
}

void Spectrograph::drawNextFrameOfSpectrum()
{
    window.multiplyWithWindowingTable(fftData, fftSize);
    forwardFFT.performFrequencyOnlyForwardTransform(fftData);

    auto mindB = -100.0f;
    auto maxdB =    0.0f;

    float sampleRate = processor.getSampleRate();
    if (sampleRate <= 0.0f) sampleRate = 44100.0f;
    float baseFreq = getEffectiveBaseFreq();

    if (auto* rParam = processor.apvts.getParameter("RATIO1"))
    {
        for (int i = 0; i < scopeSize; ++i)
        {
            float freq = 0.0f;
            float norm = (float)i / (float)(scopeSize - 1);
            if (scaleMode == SpectrumScaleMode::Full) {
                // Log scale from 20 Hz to 20000 Hz
                freq = 20.0f * std::pow(20000.0f / 20.0f, norm);
            } else {
                float ratio = rParam->convertFrom0to1(norm);
                freq = ratio * baseFreq;
            }
            
            int fftDataIndex = (int)(freq * (float)fftSize / sampleRate);
            fftDataIndex = juce::jlimit(0, fftSize / 2, fftDataIndex);

            auto level = juce::jmap(juce::jlimit(mindB, maxdB, juce::Decibels::gainToDecibels(fftData[fftDataIndex])
                                                                - juce::Decibels::gainToDecibels((float)fftSize)),
                                    mindB, maxdB, 0.0f, 1.0f);

            scopeData[i] = level;
        }
    }
}

namespace
{
    struct PaletteStop
    {
        float pos;
        juce::uint8 r, g, b;
    };

    // 1. Spectrum (Full rainbow spectrum: Deep navy -> Blue -> Cyan -> Green -> Yellow -> Orange -> Red -> Magenta -> White)
    static const PaletteStop spectrumStops[] = {
        { 0.00f,   4,   4,  20 }, // Noise floor: deep dark midnight navy
        { 0.10f,   0,  20, 110 }, // Deep blue
        { 0.22f,   0,  70, 205 }, // Royal blue
        { 0.36f,   0, 185, 230 }, // Electric cyan
        { 0.48f,   0, 215, 140 }, // Teal / Sea green
        { 0.60f,  30, 220,  35 }, // Bright emerald green
        { 0.72f, 255, 230,   0 }, // Vivid yellow
        { 0.84f, 255, 105,   0 }, // Amber orange
        { 0.92f, 240,  20,  20 }, // Fiery red
        { 0.97f, 255,  50, 190 }, // Electric magenta
        { 1.00f, 255, 255, 255 }  // Saturated white
    };

    // 2. Aurora (Vivid rainbow colormap)
    static const PaletteStop auroraStops[] = {
        { 0.00f,  34,  48, 141 },
        { 0.15f,  61, 112, 233 },
        { 0.30f,  38, 189, 219 },
        { 0.45f,  68, 237, 137 },
        { 0.60f, 175, 240,  48 },
        { 0.75f, 251, 185,  34 },
        { 0.90f, 238,  81,  22 },
        { 1.00f, 122,   4,   3 }
    };

    // 3. Thermal (Heat glow: Black -> Violet -> Crimson -> Orange -> Yellow -> White)
    static const PaletteStop thermalStops[] = {
        { 0.00f,   4,   2,  12 },
        { 0.18f,  60,  12, 110 },
        { 0.38f, 145,  22, 120 },
        { 0.58f, 220,  60,  45 },
        { 0.78f, 255, 185,  25 },
        { 0.94f, 255, 245, 140 },
        { 1.00f, 255, 255, 255 }
    };

    // 4. Ocean (Abyssal blue -> Cyan -> White)
    static const PaletteStop oceanStops[] = {
        { 0.00f,   2,   6,  20 },
        { 0.22f,   8,  35,  95 },
        { 0.45f,  12, 105, 180 },
        { 0.70f,  35, 210, 230 },
        { 0.90f, 160, 245, 255 },
        { 1.00f, 255, 255, 255 }
    };

    // 5. Monochrome (Grayscale: Black -> White)
    static const PaletteStop monoStops[] = {
        { 0.00f,   0,   0,   0 },
        { 1.00f, 255, 255, 255 }
    };

    inline juce::Colour interpolateStops(float t, const PaletteStop* stops, int count)
    {
        t = juce::jlimit(0.0f, 1.0f, t);
        if (t <= stops[0].pos)
            return juce::Colour(stops[0].r, stops[0].g, stops[0].b);
        if (t >= stops[count - 1].pos)
            return juce::Colour(stops[count - 1].r, stops[count - 1].g, stops[count - 1].b);

        for (int i = 0; i < count - 1; ++i)
        {
            if (t >= stops[i].pos && t <= stops[i + 1].pos)
            {
                float span = stops[i + 1].pos - stops[i].pos;
                float f = (span > 0.00001f) ? ((t - stops[i].pos) / span) : 0.0f;
                float smoothF = f * f * (3.0f - 2.0f * f);
                auto r = (juce::uint8)juce::jlimit(0, 255, (int)std::round(stops[i].r + smoothF * ((float)stops[i + 1].r - (float)stops[i].r)));
                auto g = (juce::uint8)juce::jlimit(0, 255, (int)std::round(stops[i].g + smoothF * ((float)stops[i + 1].g - (float)stops[i].g)));
                auto b = (juce::uint8)juce::jlimit(0, 255, (int)std::round(stops[i].b + smoothF * ((float)stops[i + 1].b - (float)stops[i].b)));
                return juce::Colour(r, g, b);
            }
        }
        return juce::Colour(stops[count - 1].r, stops[count - 1].g, stops[count - 1].b);
    }
}

void Spectrograph::buildWaterfallLut()
{
    const PaletteStop* stops = spectrumStops;
    int stopCount = (int)std::size(spectrumStops);

    switch (currentPalette)
    {
        case WaterfallPalette::Spectrum:
            stops = spectrumStops;
            stopCount = (int)std::size(spectrumStops);
            break;
        case WaterfallPalette::Aurora:
            stops = auroraStops;
            stopCount = (int)std::size(auroraStops);
            break;
        case WaterfallPalette::Thermal:
            stops = thermalStops;
            stopCount = (int)std::size(thermalStops);
            break;
        case WaterfallPalette::Ocean:
            stops = oceanStops;
            stopCount = (int)std::size(oceanStops);
            break;
        case WaterfallPalette::Monochrome:
            stops = monoStops;
            stopCount = (int)std::size(monoStops);
            break;
        default:
            break;
    }

    for (int i = 0; i < 1024; ++i)
    {
        float t = (float)i / 1023.0f;
        waterfallLut[(size_t)i] = interpolateStops(t, stops, stopCount);
    }
}

void Spectrograph::setWaterfallPalette(WaterfallPalette palette)
{
    currentPalette = palette;
    buildWaterfallLut();
    regenerateWaterfallImage();
    repaint();
}

void Spectrograph::cycleWaterfallPalette()
{
    int next = (static_cast<int>(currentPalette) + 1) % static_cast<int>(WaterfallPalette::NumPalettes);
    setWaterfallPalette(static_cast<WaterfallPalette>(next));
}

void Spectrograph::regenerateWaterfallImage()
{
    if (!waterfallImage.isValid()) return;
    int imgW = waterfallImage.getWidth();
    int imgH = waterfallImage.getHeight();
    if ((int)waterfallHistory.size() != imgH * scopeSize)
        return;

    juce::Image::BitmapData bmd(waterfallImage, juce::Image::BitmapData::readWrite);
    for (int y = 0; y < imgH; ++y)
    {
        const float* row = &waterfallHistory[(size_t)(y * scopeSize)];
        for (int x = 0; x < imgW; ++x)
        {
            float normX = (float)x / (float)(imgW - 1);
            int bin = juce::jlimit(0, scopeSize - 1, (int)(normX * (float)(scopeSize - 1)));
            float level = row[bin];
            bmd.setPixelColour(x, y, waterfallColour(level));
        }
    }
}

// Maps a 0..1 level to a color value using the calibrated LUT
juce::Colour Spectrograph::waterfallColour(float level) const
{
    level = juce::jlimit(0.0f, 1.0f, level);
    float curved = std::pow(level, 1.25f);
    int idx = juce::jlimit(0, 1023, (int)(curved * 1023.0f));
    return waterfallLut[(size_t)idx];
}

// Push one new row of FFT data into the waterfall image and advance write pointer (called from timerCallback)
void Spectrograph::updateWaterfallRow()
{
    if (!isWaterfallMode)
        return;

    auto plotBounds = getGraphPlotBounds();
    int imgW = juce::jmax(1, (int)plotBounds.getWidth());
    int imgH = juce::jmax(1, (int)plotBounds.getHeight());

    // Allocate / resize image lazily
    if (waterfallImage.getWidth()  != imgW ||
        waterfallImage.getHeight() != imgH ||
        !waterfallImage.isValid())
    {
        waterfallImage = juce::Image(juce::Image::RGB, imgW, imgH, true);
        waterfallImage.clear(waterfallImage.getBounds(), waterfallColour(0.0f));
        waterfallHistory.assign((size_t)(imgH * scopeSize), 0.0f);
        waterfallLineY = 0;
    }

    // Write new frequency row at waterfallLineY (top-down scroll)
    if ((int)waterfallHistory.size() == imgH * scopeSize)
    {
        float* row = &waterfallHistory[(size_t)(waterfallLineY * scopeSize)];
        for (int i = 0; i < scopeSize; ++i)
            row[i] = scopeData[i];
    }

    juce::Image::BitmapData bmd(waterfallImage, juce::Image::BitmapData::readWrite);
    int y = waterfallLineY;
    for (int x = 0; x < imgW; ++x)
    {
        float normX = (float)x / (float)(imgW - 1);
        int bin = juce::jlimit(0, scopeSize - 1, (int)(normX * (float)(scopeSize - 1)));
        float level = scopeData[bin];
        auto col = waterfallColour(level);
        bmd.setPixelColour(x, y, col);
    }
    waterfallLineY = (waterfallLineY + 1) % imgH;
}

// Paint waterfall - strictly read-only display
void Spectrograph::paintWaterfall(juce::Graphics& g, float left, float top, float width, float height)
{
    if (!waterfallImage.isValid())
        return;

    int imgW = waterfallImage.getWidth();
    int imgH = waterfallImage.getHeight();
    if (imgW <= 0 || imgH <= 0)
        return;

    // Draw the image split at waterfallLineY so it appears as continuous scroll
    // Upper portion: rows waterfallLineY..imgH-1  -> drawn at the top of the display area
    // Lower portion: rows 0..waterfallLineY-1     -> drawn below
    int splitY = waterfallLineY;
    int upperH = imgH - splitY;   // rows below the write pointer (older)
    int lowerH = splitY;          // rows above the write pointer (newer)

    // Older rows (above split in image) -> top of screen
    if (upperH > 0)
    {
        juce::Rectangle<int> srcUpper(0, splitY, imgW, upperH);
        g.drawImage(waterfallImage,
                    (int)left, (int)top, imgW, upperH,
                    srcUpper.getX(), srcUpper.getY(), srcUpper.getWidth(), srcUpper.getHeight());
    }
    // Newer rows (below split in image) -> bottom of screen
    if (lowerH > 0)
    {
        juce::Rectangle<int> srcLower(0, 0, imgW, lowerH);
        g.drawImage(waterfallImage,
                    (int)left, (int)top + upperH, imgW, lowerH,
                    srcLower.getX(), srcLower.getY(), srcLower.getWidth(), srcLower.getHeight());
    }

    // Colour-scale legend on the right edge (10px wide)
    int legendW = 10;
    int legendX = (int)(left + width) - legendW - 2;
    for (int py = 0; py < imgH; ++py)
    {
        float lev = 1.0f - (float)py / (float)(imgH - 1);
        g.setColour(waterfallColour(lev));
        g.fillRect(legendX, (int)top + py, legendW, 1);
    }
    g.setColour(juce::Colours::white.withAlpha(0.6f));
    g.drawRect(legendX, (int)top, legendW, imgH, 1);

    // Calibration notches along legend
    for (int notch = 0; notch <= 4; ++notch)
    {
        int ny = (int)top + (int)std::round((float)notch * (float)(imgH - 1) / 4.0f);
        g.setColour(juce::Colours::white.withAlpha(0.85f));
        g.drawHorizontalLine(ny, (float)legendX, (float)(legendX + 3));
    }
}

void Spectrograph::updateWaterfallDots()
{
    // Check if a new note was initiated
    int currentTrigger = processor.noteTriggerCounter.load(std::memory_order_relaxed);
    if (currentTrigger != lastSeenTriggerCounter)
    {
        lastSeenTriggerCounter = currentTrigger;
        dotAnimStartTimeMs = juce::Time::getMillisecondCounter();
        isDotAnimRunning = true;

        float baseFreq = processor.lastTriggeredFrequency.load(std::memory_order_relaxed);
        if (baseFreq <= 0.0f) baseFreq = 440.0f;

        auto plotBounds = getGraphPlotBounds();
        float left = plotBounds.getX();
        float width = plotBounds.getWidth();

        auto* rParam = processor.apvts.getParameter("RATIO1");

        // Calculate fundamental root note position
        float baseX = left;
        if (scaleMode == SpectrumScaleMode::Full)
        {
            float baseNormLog = std::log10(juce::jmax(20.0f, baseFreq) / 20.0f) / std::log10(20000.0f / 20.0f);
            baseX = left + juce::jlimit(0.0f, 1.0f, baseNormLog) * width;
        }
        else
        {
            float normBase = rParam ? rParam->convertTo0to1(1.0f) : 0.0f;
            baseX = left + juce::jlimit(0.0f, 1.0f, normBase) * width;
        }

        int numActive = (int)processor.apvts.getRawParameterValue("NUM_PARTIALS")->load();
        numActive = juce::jlimit(1, 16, numActive);

        for (int i = 0; i < 16; ++i)
        {
            if (i < numActive)
            {
                auto idSuffix = juce::String(i + 1);
                float ratioVal = 1.0f;
                if (auto* rp = processor.apvts.getParameter("RATIO" + idSuffix))
                    ratioVal = rp->convertFrom0to1(rp->getValue());

                float semiVal = 0.0f;
                if (auto* sp = processor.apvts.getParameter("SEMITONE" + idSuffix))
                    semiVal = sp->convertFrom0to1(sp->getValue());

                float effRatio = ratioVal * std::pow(2.0f, semiVal / 12.0f);
                float harmFreq = baseFreq * effRatio;

                float targetX = left;
                if (scaleMode == SpectrumScaleMode::Full)
                {
                    float normLog = std::log10(juce::jmax(20.0f, harmFreq) / 20.0f) / std::log10(20000.0f / 20.0f);
                    targetX = left + juce::jlimit(0.0f, 1.0f, normLog) * width;
                }
                else
                {
                    float normRatio = rParam ? rParam->convertTo0to1(juce::jlimit(0.01f, 32.0f, effRatio)) : 0.0f;
                    targetX = left + juce::jlimit(0.0f, 1.0f, normRatio) * width;
                }

                // Start position: current position if already visible, otherwise start at root note baseX
                if (waterfallDots[i].hasValidPos && dotAnimAlpha > 0.05f)
                    waterfallDots[i].startX = waterfallDots[i].currentX;
                else
                    waterfallDots[i].startX = baseX;

                waterfallDots[i].targetX = targetX;
                waterfallDots[i].freqHz = harmFreq;
                waterfallDots[i].hasValidPos = true;
            }
            else
            {
                waterfallDots[i].hasValidPos = false;
            }
        }
    }

    if (isDotAnimRunning)
    {
        juce::uint32 elapsedMs = juce::Time::getMillisecondCounter() - dotAnimStartTimeMs;
        const float moveDurationMs = 380.0f;
        const float holdDurationMs = 250.0f;
        const float fadeDurationMs = 450.0f;
        const float totalDurationMs = moveDurationMs + holdDurationMs + fadeDurationMs; // 1080 ms

        if (elapsedMs < moveDurationMs)
        {
            // Moving smoothly to target frequency
            float t = (float)elapsedMs / moveDurationMs;
            float ease = 1.0f - std::pow(1.0f - t, 3.0f); // smooth cubic ease-out
            for (auto& dot : waterfallDots)
            {
                if (dot.hasValidPos)
                    dot.currentX = dot.startX + (dot.targetX - dot.startX) * ease;
            }
            dotAnimAlpha = 1.0f;
        }
        else if (elapsedMs < moveDurationMs + holdDurationMs)
        {
            // Reached target frequency, resting clearly
            for (auto& dot : waterfallDots)
            {
                if (dot.hasValidPos)
                    dot.currentX = dot.targetX;
            }
            dotAnimAlpha = 1.0f;
        }
        else if (elapsedMs < totalDurationMs)
        {
            // Fading out into transparency
            for (auto& dot : waterfallDots)
            {
                if (dot.hasValidPos)
                    dot.currentX = dot.targetX;
            }
            float fadeProgress = (float)(elapsedMs - (moveDurationMs + holdDurationMs)) / fadeDurationMs;
            dotAnimAlpha = juce::jmax(0.0f, 1.0f - fadeProgress);
        }
        else
        {
            // Finished and disappeared
            for (auto& dot : waterfallDots)
            {
                if (dot.hasValidPos)
                    dot.currentX = dot.targetX;
            }
            dotAnimAlpha = 0.0f;
            isDotAnimRunning = false;
        }
    }
}

void Spectrograph::paintWaterfallDots(juce::Graphics& g, float left, float top, float width, float height)
{
    if (dotAnimAlpha <= 0.005f)
        return;

    float dotTrackY = top + height + 26.0f;

    // Subtle track line
    g.setColour(juce::Colour(0xFF282835).withAlpha(dotAnimAlpha * 0.7f));
    g.drawHorizontalLine((int)dotTrackY, left, left + width);

    int numActive = (int)processor.apvts.getRawParameterValue("NUM_PARTIALS")->load();
    numActive = juce::jlimit(1, 16, numActive);

    for (int i = 0; i < numActive; ++i)
    {
        if (!waterfallDots[i].hasValidPos) continue;

        float dotX = waterfallDots[i].currentX;
        if (dotX < left - 15.0f || dotX > left + width + 15.0f) continue;

        auto col = getPartialColour(i);

        // Thin vertical guide beam connecting to the bottom of the waterfall
        g.setColour(col.withAlpha(dotAnimAlpha * 0.35f));
        g.drawLine(dotX, top + height, dotX, dotTrackY, 1.0f);

        // Soft outer glow halo
        float haloRadius = 8.5f;
        g.setColour(col.withAlpha(dotAnimAlpha * 0.32f));
        g.fillEllipse(dotX - haloRadius, dotTrackY - haloRadius, haloRadius * 2.0f, haloRadius * 2.0f);

        // Vibrant colored jewel dot
        float dotRadius = 4.5f;
        g.setColour(col.withAlpha(dotAnimAlpha));
        g.fillEllipse(dotX - dotRadius, dotTrackY - dotRadius, dotRadius * 2.0f, dotRadius * 2.0f);

        // Bright white specular core
        float coreRadius = 1.8f;
        g.setColour(juce::Colours::white.withAlpha(dotAnimAlpha * 0.9f));
        g.fillEllipse(dotX - coreRadius, dotTrackY - coreRadius, coreRadius * 2.0f, coreRadius * 2.0f);

        // Partial index label underneath dot
        g.setFont(juce::Font(8.0f, juce::Font::bold));
        g.setColour(col.brighter(0.2f).withAlpha(dotAnimAlpha * 0.9f));
        juce::String labelStr = juce::String(i + 1);
        g.drawText(labelStr, (int)dotX - 10, (int)dotTrackY + 5, 20, 10, juce::Justification::centred);
    }
}

juce::Point<float> Spectrograph::mapParamsToScreen(float effRatio, float gain, float semi, float detune,
                                                   float left, float top, float width, float height, float baseFreq) const
{
    juce::ignoreUnused(semi, detune);

    float px = left;
    if (scaleMode == SpectrumScaleMode::Full)
    {
        float harmFreq = juce::jmax(20.0f, effRatio * baseFreq);
        float normStatic = juce::jlimit(0.0f, 1.0f, std::log10(harmFreq / 20.0f) / std::log10(20000.0f / 20.0f));
        px = left + normStatic * width;
    }
    else
    {
        float normX = 0.0f;
        if (auto* rp = processor.apvts.getParameter("RATIO1"))
            normX = rp->convertTo0to1(juce::jlimit(0.01f, 32.0f, effRatio));
        px = left + normX * width;
    }

    float py = (top + height) - juce::jlimit(0.0f, 1.0f, gain) * height;
    return { px, py };
}

Spectrograph::ComputedPartialCoords Spectrograph::computePartialCoords(int partialIdx, float left, float top, float width, float height, float baseFreq) const
{
    ComputedPartialCoords res;
    partialIdx = juce::jlimit(0, 15, partialIdx);
    juce::String s = juce::String(partialIdx + 1);

    float baseRatio  = 1.0f;
    float baseGain   = 0.0f;
    float baseSemi   = 0.0f;
    float baseDetune = 0.0f;
    float basePan    = 0.0f;

    if (auto* p = processor.apvts.getRawParameterValue("RATIO" + s))    baseRatio  = p->load();
    if (auto* p = processor.apvts.getRawParameterValue("GAIN" + s))     baseGain   = p->load();
    if (auto* p = processor.apvts.getRawParameterValue("SEMITONE" + s)) baseSemi   = p->load();
    if (auto* p = processor.apvts.getRawParameterValue("DETUNE" + s))   baseDetune = p->load();
    if (auto* p = processor.apvts.getRawParameterValue("PAN" + s))      basePan    = p->load();

    int animMode = 0;
    int animCurve = 0;
    float animDepth = 1.0f;
    if (auto* p = processor.apvts.getRawParameterValue("ANIM_MODE" + s))  animMode  = (int)p->load();
    if (auto* p = processor.apvts.getRawParameterValue("ANIM_CURVE" + s)) animCurve = (int)p->load();
    if (auto* p = processor.apvts.getRawParameterValue("ANIM_DEPTH" + s)) animDepth = p->load();

    res.anchorRatio  = baseRatio;
    res.anchorGain   = baseGain;
    res.anchorSemi   = baseSemi;
    res.anchorDetune = 0.0f;
    res.anchorPan    = basePan;

    PartialAnimationData track = processor.getPartialAnimation(partialIdx);
    bool voiceActive = processor.anyVoiceActiveVis.load(std::memory_order_relaxed);
    float evalT = 0.0f;
    if (voiceActive)
        evalT = processor.livePartialState[partialIdx].animPhase.load(std::memory_order_relaxed);
    else if (!isLocked && animMode > 0)
        evalT = previewAnimPhases[partialIdx];
    else if (isTimelineOpen)
        evalT = scrubTimeNorm;

    if (animMode > 0 && track.numKeyframes > 0)
    {
        const PartialKeyframe& startKf = track.keyframes[0];
        PartialKeyframe evalKf = track.evaluate(evalT, animCurve, startKf);
        float effDepth = (isLocked || draggedPartial == partialIdx || isTimelineOpen) ? 1.0f : animDepth;
        res.anchorRatio  = juce::jlimit(0.01f, 32.0f,  startKf.ratio    + effDepth * (evalKf.ratio    - startKf.ratio));
        res.anchorGain   = juce::jlimit(0.0f,  1.0f,   startKf.gain     + effDepth * (evalKf.gain     - startKf.gain));
        res.anchorSemi   = juce::jlimit(-24.0f, 24.0f, startKf.semitone + effDepth * (evalKf.semitone - startKf.semitone));
        res.anchorDetune = 0.0f;
        res.anchorPan    = juce::jlimit(-1.0f,  1.0f,   startKf.pan      + effDepth * (evalKf.pan      - startKf.pan));
    }

    float anchorEffRatio = juce::jlimit(0.01f, 32.0f, res.anchorRatio * std::pow(2.0f, res.anchorSemi / 12.0f));
    res.anchorPos = mapParamsToScreen(anchorEffRatio, res.anchorGain, res.anchorSemi, 0.0f, left, top, width, height, baseFreq);

    if (isLocked || draggedPartial == partialIdx)
    {
        res.modRatio  = anchorEffRatio;
        res.modGain   = res.anchorGain;
        res.modSemi   = res.anchorSemi;
        res.modDetune = 0.0f;
        res.modPan    = res.anchorPan;
        res.livePos   = res.anchorPos;
    }
    else
    {
        res.modRatio  = smoothedModRatio[partialIdx];
        res.modGain   = smoothedModGain[partialIdx];
        res.modSemi   = smoothedModSemi[partialIdx];
        res.modDetune = 0.0f;
        res.modPan    = smoothedModPan[partialIdx];
        res.livePos   = mapParamsToScreen(res.modRatio, res.modGain, res.modSemi, 0.0f, left, top, width, height, baseFreq);
    }

    return res;
}

int Spectrograph::findKeyframeAtTimelinePos(float mouseX, float mouseY, juce::Rectangle<float> trackBounds) const
{
    if (!trackBounds.expanded(8.0f, 6.0f).contains(mouseX, mouseY))
        return -1;

    PartialAnimationData track = processor.getPartialAnimation(selectedPartial);
    float bestDist = 10.0f;
    int bestIdx = -1;
    for (int k = 0; k < track.numKeyframes; ++k)
    {
        float kx = trackBounds.getX() + track.keyframes[k].time * trackBounds.getWidth();
        float dist = std::abs(mouseX - kx);
        if (dist <= bestDist)
        {
            bestDist = dist;
            bestIdx = k;
        }
    }
    return bestIdx;
}

int Spectrograph::findWaypointDiamondAt(float mouseX, float mouseY, int& outPartialIdx) const
{
    outPartialIdx = -1;
    auto plotBounds = getGraphPlotBounds();
    float left   = plotBounds.getX();
    float top    = plotBounds.getY();
    float width  = plotBounds.getWidth();
    float height = plotBounds.getHeight();

    if (!plotBounds.expanded(10.0f, 10.0f).contains(mouseX, mouseY))
        return -1;

    int numActive = (int)processor.apvts.getRawParameterValue("NUM_PARTIALS")->load();
    numActive = juce::jlimit(1, 16, numActive);

    float baseFreq = getEffectiveBaseFreq();

    auto checkDiamondsForPartial = [&](int pIdx, float maxDist) -> int
    {
        if (pIdx < 0 || pIdx >= numActive) return -1;
        juce::String s = juce::String(pIdx + 1);
        if (auto* ep = processor.apvts.getRawParameterValue("P_ENABLED" + s))
            if (ep->load() <= 0.5f) return -1;

        int animMode = 0;
        float animDepth = 1.0f;
        if (auto* p = processor.apvts.getRawParameterValue("ANIM_MODE" + s))  animMode  = (int)p->load();
        if (auto* p = processor.apvts.getRawParameterValue("ANIM_DEPTH" + s)) animDepth = p->load();

        PartialAnimationData track = processor.getPartialAnimation(pIdx);
        if (track.numKeyframes <= 0 || animMode <= 0)
            return -1;

        const PartialKeyframe& startKf = track.keyframes[0];
        float effDepth = (animMode > 0) ? animDepth : 1.0f;

        int bestK = -1;
        float bestD = maxDist;
        for (int k = track.numKeyframes - 1; k >= 0; --k)
        {
            const auto& kf = track.keyframes[k];
            float r  = juce::jlimit(0.01f, 32.0f,  startKf.ratio    + effDepth * (kf.ratio    - startKf.ratio));
            float gn = juce::jlimit(0.0f,  1.0f,   startKf.gain     + effDepth * (kf.gain     - startKf.gain));
            float sm = juce::jlimit(-24.0f, 24.0f, startKf.semitone + effDepth * (kf.semitone - startKf.semitone));
            float effR = juce::jlimit(0.01f, 32.0f, r * std::pow(2.0f, sm / 12.0f));
            auto pt = mapParamsToScreen(effR, gn, sm, 0.0f, left, top, width, height, baseFreq);
            float d = std::hypot(mouseX - pt.x, mouseY - pt.y);
            if (d <= bestD)
            {
                bestD = d;
                bestK = k;
            }
        }
        return bestK;
    };

    int hitK = checkDiamondsForPartial(selectedPartial, 10.0f);
    if (hitK >= 0)
    {
        outPartialIdx = selectedPartial;
        return hitK;
    }

    for (int i = numActive - 1; i >= 0; --i)
    {
        if (i == selectedPartial) continue;
        hitK = checkDiamondsForPartial(i, 9.0f);
        if (hitK >= 0)
        {
            outPartialIdx = i;
            return hitK;
        }
    }
    return -1;
}

void Spectrograph::paintKeyframeTimeline(juce::Graphics& g, juce::Rectangle<float> timelineArea)
{
    // Standalone Timeline Panel container (matching Spectrograph / Oscilloscope styling)
    g.setColour(juce::Colour(0xFF0A0A0A));
    g.fillRoundedRectangle(timelineArea, 6.0f);
    g.setColour(juce::Colour(0xFF333333));
    g.drawRoundedRectangle(timelineArea.reduced(0.5f), 6.0f, 1.0f);

    auto selCol = getPartialColour(selectedPartial);

    // Panel Title on top-left
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(11.0f, juce::Font::bold));
    g.drawText("TIMELINE", (int)timelineArea.getX() + 8, (int)timelineArea.getY() + 3, 58, 22, juce::Justification::centredLeft);

    auto trackRect = getTimelineTrackBounds();
    g.setColour(juce::Colour(0xFF09090E));
    g.fillRoundedRectangle(trackRect, 4.0f);
    g.setColour(juce::Colour(0xFF282834));
    g.drawRoundedRectangle(trackRect, 4.0f, 1.0f);

    if (selectedPartial < 0)
    {
        g.setColour(juce::Colours::white.withAlpha(0.35f));
        g.setFont(9.5f);
        g.drawText("No partial selected - Click a harmonic node or strip to edit keyframes",
                   trackRect.toNearestInt(), juce::Justification::centred);
        return;
    }

    juce::String s = juce::String(selectedPartial + 1);
    int animMode = 0;
    float tA = 0.01f, tD = 0.30f, tR = 0.50f;
    float timelineLen = getEffectiveTimelineInfo(selectedPartial, animMode, tA, tD, tR);

    int animCurve = 0;
    if (auto* cp = processor.apvts.getRawParameterValue("ANIM_CURVE" + s))
        animCurve = (int)cp->load();

    if (animMode <= 1)
    {
        // Note-Length relative timeline (mapped to ADSR stages: ATK -> DEC -> SUS hold -> REL)
        float totalEnv = juce::jmax(0.003f, tA + tD + tR);
        float atkEndNorm = tA / totalEnv;
        float susNorm    = (tA + tD) / totalEnv;

        float xAtk = trackRect.getX() + atkEndNorm * trackRect.getWidth();
        float xSus = trackRect.getX() + susNorm    * trackRect.getWidth();

        // Subtle ADSR stage tint bands
        g.setColour(selCol.withAlpha(0.05f));
        g.fillRect(trackRect.getX(), trackRect.getY() + 1.0f, xAtk - trackRect.getX(), trackRect.getHeight() - 2.0f);
        g.setColour(juce::Colours::white.withAlpha(0.04f));
        g.fillRect(xAtk, trackRect.getY() + 1.0f, xSus - xAtk, trackRect.getHeight() - 2.0f);

        // Attack/Decay boundary line
        g.setColour(juce::Colour(0x28FFFFFF));
        g.drawVerticalLine((int)xAtk, trackRect.getY() + 1.0f, trackRect.getBottom() - 1.0f);

        // Sustain Hold line (highlighted)
        g.setColour(juce::Colours::white.withAlpha(0.6f));
        g.drawVerticalLine((int)xSus, trackRect.getY() + 1.0f, trackRect.getBottom() - 1.0f);

        g.setFont(8.0f);
        g.setColour(juce::Colours::white.withAlpha(0.42f));
        g.drawText("0% (ON)", (int)trackRect.getX() + 3, (int)trackRect.getBottom() - 10, 44, 10, juce::Justification::centredLeft);

        if (xAtk - trackRect.getX() > 26.0f)
            g.drawText("ATK", (int)((trackRect.getX() + xAtk) * 0.5f) - 14, (int)trackRect.getY() + 1, 28, 9, juce::Justification::centred);
        if (xSus - xAtk > 26.0f)
            g.drawText("DEC", (int)((xAtk + xSus) * 0.5f) - 14, (int)trackRect.getY() + 1, 28, 9, juce::Justification::centred);
        if (trackRect.getRight() - xSus > 26.0f)
            g.drawText("REL", (int)((xSus + trackRect.getRight()) * 0.5f) - 14, (int)trackRect.getY() + 1, 28, 9, juce::Justification::centred);

        g.setColour(juce::Colours::white.withAlpha(0.75f));
        int susLabelX = juce::jlimit((int)trackRect.getX() + 44, (int)trackRect.getRight() - 88, (int)xSus - 28);
        g.drawText("SUS HOLD", susLabelX, (int)trackRect.getBottom() - 10, 56, 10, juce::Justification::centred);

        g.setColour(juce::Colours::white.withAlpha(0.42f));
        juce::String endLbl = "Note: " + juce::String(totalEnv, totalEnv >= 10.0f ? 1 : 2) + "s";
        g.drawText(endLbl, (int)trackRect.getRight() - 62, (int)trackRect.getBottom() - 10, 60, 10, juce::Justification::centredRight);
    }
    else
    {
        // Time-based modulation grid divisions (0%, 25%, 50%, 75%, 100%)
        for (int div = 0; div <= 4; ++div)
        {
            float normT = (float)div / 4.0f;
            float gx = trackRect.getX() + normT * trackRect.getWidth();
            if (div > 0 && div < 4)
            {
                bool isPingPongTurn = (animMode == 4 && div == 2);
                g.setColour(isPingPongTurn ? juce::Colour(0x66FFB300) : juce::Colour(0x18FFFFFF));
                g.drawVerticalLine((int)gx, trackRect.getY() + 1.0f, trackRect.getBottom() - 1.0f);
                if (isPingPongTurn)
                {
                    g.setFont(7.5f);
                    g.drawText("TURN", (int)gx - 18, (int)trackRect.getY() + 1, 36, 9, juce::Justification::centred);
                }
            }
            g.setColour(juce::Colours::white.withAlpha(0.38f));
            g.setFont(8.0f);
            float sec = normT * timelineLen;
            juce::String tLabel = juce::String(sec, sec >= 10.0f ? 1 : 2) + "s";
            int textX = (div == 0) ? (int)gx + 2 : ((div == 4) ? (int)gx - 34 : (int)gx - 17);
            g.drawText(tLabel, textX, (int)trackRect.getBottom() - 10, 34, 10,
                       div == 0 ? juce::Justification::centredLeft : (div == 4 ? juce::Justification::centredRight : juce::Justification::centred));
        }
    }

    // Draw interpolated parameter curves inside the timeline track
    PartialAnimationData track = processor.getPartialAnimation(selectedPartial);
    PartialKeyframe baseKf = processor.getPartialBaseSettings(selectedPartial);

    if (animMode > 0 && track.numKeyframes > 0)
    {
        const PartialKeyframe& startKf = track.keyframes[0];
        juce::Path primaryCurve, primaryFill, secondaryCurve;
        const int steps = 64;
        auto* rp = processor.apvts.getParameter("RATIO1");

        for (int step = 0; step <= steps; ++step)
        {
            float t = (float)step / (float)steps;
            PartialKeyframe eval = track.evaluate(t, animCurve, startKf);

            float normPrimary   = juce::jlimit(0.0f, 1.0f, eval.gain);
            float effRatio      = juce::jlimit(0.01f, 32.0f, eval.ratio * std::pow(2.0f, eval.semitone / 12.0f));
            float normSecondary = rp ? rp->convertTo0to1(effRatio) : 0.5f;

            float x = trackRect.getX() + t * trackRect.getWidth();
            float y1 = trackRect.getBottom() - 3.0f - normPrimary * (trackRect.getHeight() - 6.0f);
            float y2 = trackRect.getBottom() - 3.0f - normSecondary * (trackRect.getHeight() - 6.0f);

            if (step == 0)
            {
                primaryCurve.startNewSubPath(x, y1);
                primaryFill.startNewSubPath(x, trackRect.getBottom() - 1.0f);
                primaryFill.lineTo(x, y1);
                secondaryCurve.startNewSubPath(x, y2);
            }
            else
            {
                primaryCurve.lineTo(x, y1);
                primaryFill.lineTo(x, y1);
                secondaryCurve.lineTo(x, y2);
            }
        }
        primaryFill.lineTo(trackRect.getRight(), trackRect.getBottom() - 1.0f);
        primaryFill.closeSubPath();

        g.setColour(selCol.withAlpha(0.16f));
        g.fillPath(primaryFill);

        g.setColour(juce::Colours::white.withAlpha(0.45f));
        g.strokePath(secondaryCurve, juce::PathStrokeType(1.1f));

        g.setColour(selCol.withAlpha(0.9f));
        g.strokePath(primaryCurve, juce::PathStrokeType(1.6f));
    }
    else
    {
        g.setColour(juce::Colours::white.withAlpha(0.32f));
        g.setFont(9.5f);
        g.drawText(animMode == 0
                       ? "Animation OFF - Click ON or select a Mode to keyframe H" + juce::String(selectedPartial + 1)
                       : "Double-click timeline or drag graph nodes to add keyframes",
                   trackRect.toNearestInt(), juce::Justification::centred);
    }

    // Subtle ticks along the top edge for other partials that have keyframes
    int numActive = (int)processor.apvts.getRawParameterValue("NUM_PARTIALS")->load();
    numActive = juce::jlimit(1, 16, numActive);
    for (int p = 0; p < numActive; ++p)
    {
        if (p == selectedPartial) continue;
        int otherMode = 0;
        if (auto* mp = processor.apvts.getRawParameterValue("ANIM_MODE" + juce::String(p + 1)))
            otherMode = (int)mp->load();
        if (otherMode <= 0) continue;
        PartialAnimationData otherTrack = processor.getPartialAnimation(p);
        if (otherTrack.numKeyframes <= 0) continue;
        auto pCol = getPartialColour(p).withAlpha(0.55f);
        g.setColour(pCol);
        for (int k = 0; k < otherTrack.numKeyframes; ++k)
        {
            float kx = trackRect.getX() + otherTrack.keyframes[k].time * trackRect.getWidth();
            g.fillEllipse(kx - 2.0f, trackRect.getY() + 2.0f, 4.0f, 4.0f);
        }
    }

    // Draw Keyframe Diamonds for selectedPartial when animation is enabled
    if (animMode > 0)
    {
        float cy = trackRect.getCentreY();
        for (int k = 0; k < track.numKeyframes; ++k)
        {
            float kx = trackRect.getX() + track.keyframes[k].time * trackRect.getWidth();
            bool isSelKf = (k == selectedKeyframeIndex);
            float r = isSelKf ? 6.5f : 5.2f;

            juce::Path diamond;
            diamond.startNewSubPath(kx, cy - r);
            diamond.lineTo(kx + r, cy);
            diamond.lineTo(kx, cy + r);
            diamond.lineTo(kx - r, cy);
            diamond.closeSubPath();

            if (isSelKf)
            {
                g.setColour(selCol.withAlpha(0.45f));
                g.strokePath(diamond, juce::PathStrokeType(3.5f));
                g.setColour(juce::Colours::white);
                g.fillPath(diamond);
                g.setColour(selCol);
                g.strokePath(diamond, juce::PathStrokeType(1.6f));
            }
            else
            {
                g.setColour(selCol);
                g.fillPath(diamond);
                g.setColour(juce::Colours::white.withAlpha(0.85f));
                g.strokePath(diamond, juce::PathStrokeType(1.2f));
            }
        }
    }

    // Draw Playhead
    float phX = trackRect.getX() + juce::jlimit(0.0f, 1.0f, scrubTimeNorm) * trackRect.getWidth();
    g.setColour(juce::Colour(0xFFFFD740));
    g.drawLine(phX, trackRect.getY(), phX, trackRect.getBottom(), 1.8f);

    juce::Path phCap;
    phCap.addTriangle(phX - 5.0f, trackRect.getY(), phX + 5.0f, trackRect.getY(), phX, trackRect.getY() + 6.5f);
    g.fillPath(phCap);
}

void Spectrograph::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Dark background with rounded corners
    g.setColour(juce::Colour(0xFF0A0A0A));
    g.fillRoundedRectangle(bounds, 8.0f);

    // Subtle border
    g.setColour(juce::Colour(0xFF333333));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 8.0f, 1.0f);

    auto inner = bounds.reduced(12.0f);
    auto plotBounds = getGraphPlotBounds();
    auto left   = plotBounds.getX();
    auto top    = plotBounds.getY();
    auto width  = plotBounds.getWidth();
    auto height = plotBounds.getHeight();

    float baseFreq = getEffectiveBaseFreq();

    // ---- WATERFALL or SPECTRUM BACKGROUND ------------------------------
    if (isWaterfallMode)
    {
        // Clip to graph area
        g.saveState();
        g.reduceClipRegion((int)left, (int)top, (int)width, (int)height);
        paintWaterfall(g, left, top, width, height);
        g.restoreState();

        paintWaterfallDots(g, left, top, width, height);
    }
    else
    {
        // Grid lines
        g.setColour(juce::Colour(0xFF1A1A1A));
        for (int i = 1; i < 4; ++i)
        {
            float yLine = top + height * (float)i / 4.0f;
            g.drawHorizontalLine((int)yLine, left, left + width);
        }

        // dB labels on horizontal grid lines
        g.setColour(juce::Colours::white.withAlpha(0.35f));
        g.setFont(8.5f);
        g.drawText(" 0 dB",  (int)left + 2, (int)top - 6, 36, 12, juce::Justification::topLeft);
        g.drawText("-12 dB", (int)left + 2, (int)(top + height * 0.25f) - 6, 36, 12, juce::Justification::topLeft);
        g.drawText("-24 dB", (int)left + 2, (int)(top + height * 0.50f) - 6, 36, 12, juce::Justification::topLeft);
        g.drawText("-36 dB", (int)left + 2, (int)(top + height * 0.75f) - 6, 36, 12, juce::Justification::topLeft);
    }

    // Vertical guide lines (Full Spectrum Static vs Sliding Harmonic Ratio)
    if (scaleMode == SpectrumScaleMode::Full)
    {
        const float freqs[] = { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f };
        for (float f : freqs)
        {
            float normX = std::log10(f / 20.0f) / std::log10(20000.0f / 20.0f);
            float gx = left + normX * width;
            if (normX >= 0.0f && normX <= 1.0f)
            {
                g.setColour(juce::Colour(0x18FFFFFF));
                g.drawVerticalLine((int)gx, top, top + height);
                g.setColour(juce::Colour(0x60FFFFFF));
                g.setFont(9.0f);
                juce::String label = (f >= 1000.0f) ? (juce::String((int)(f / 1000.0f)) + "k") : juce::String((int)f);
                g.drawText(label, (int)gx - 16, (int)(top + height + 1), 32, 11, juce::Justification::centred);
            }
        }
    }
    else
    {
        // Integer harmonic guide lines (1x, 2x, 3x ... 10x) with frequencies
        if (auto* rParam = processor.apvts.getParameter("RATIO1"))
        {
            for (int h = 1; h <= 10; ++h)
            {
                float norm = rParam->convertTo0to1((float)h);
                float gx = left + norm * width;

                g.setColour(juce::Colour(0x18FFFFFF));
                g.drawVerticalLine((int)gx, top, top + height);

                float harmFreq = (float)h * baseFreq;
                juce::String fStr = (harmFreq >= 1000.0f) 
                    ? (juce::String(harmFreq / 1000.0f, harmFreq >= 10000.0f ? 0 : 1) + "k")
                    : (juce::String((int)std::round(harmFreq)) + "Hz");

                g.setColour(juce::Colour(0x60FFFFFF));
                g.setFont(9.0f);
                g.drawText(juce::String(h) + "x", (int)gx - 14, (int)(top + height + 1), 28, 11, juce::Justification::centred);

                g.setColour(juce::Colour(0x35FFFFFF));
                g.setFont(8.0f);
                g.drawText(fStr, (int)gx - 20, (int)(top + height + 11), 40, 10, juce::Justification::centred);
            }
        }
    }

    if (!isWaterfallMode)
    {
        // FFT Spectrum Path
        juce::Path specPath;
        specPath.startNewSubPath(left, top + height);

        for (int i = 0; i < scopeSize; ++i)
        {
            auto x = juce::jmap((float)i, 0.0f, (float)(scopeSize - 1), left, left + width);
            auto y = juce::jmap(scopeData[i], 0.0f, 1.0f, top + height, top);
            specPath.lineTo(x, y);
        }

        specPath.lineTo(left + width, top + height);
        specPath.closeSubPath();

        juce::ColourGradient grad(juce::Colour(0x28FFFFFF), left, top,
                                  juce::Colour(0x00FFFFFF), left, top + height, false);
        g.setGradientFill(grad);
        g.fillPath(specPath);

        // Spectrum glow & line
        juce::Path linePath;
        linePath.startNewSubPath(left, juce::jmap(scopeData[0], 0.0f, 1.0f, top + height, top));
        for (int i = 1; i < scopeSize; ++i)
        {
            auto x = juce::jmap((float)i, 0.0f, (float)(scopeSize - 1), left, left + width);
            auto y = juce::jmap(scopeData[i], 0.0f, 1.0f, top + height, top);
            linePath.lineTo(x, y);
        }

        g.setColour(juce::Colour(0x22FFFFFF));
        g.strokePath(linePath, juce::PathStrokeType(4.0f));
        g.setColour(juce::Colour(0xAAFFFFFF));
        g.strokePath(linePath, juce::PathStrokeType(1.2f));
    }

    if (!isReadOnly)
    {
        int numActive = (int)processor.apvts.getRawParameterValue("NUM_PARTIALS")->load();
        numActive = juce::jlimit(1, 16, numActive);

        int activeTarget = (draggedPartial >= 0) ? draggedPartial : hoveredPartial;
        if (activeTarget < 0 && selectedPartial >= 0 && selectedPartial < numActive)
            activeTarget = selectedPartial;

        std::array<ComputedPartialCoords, 16> coords;
        for (int i = 0; i < numActive; ++i)
            coords[i] = computePartialCoords(i, left, top, width, height, baseFreq);

        // 1. Draw Keyframe Motion Paths & Waypoint Diamonds on the Graph
        for (int i = 0; i < numActive; ++i)
        {
            juce::String s = juce::String(i + 1);
            if (auto* ep = processor.apvts.getRawParameterValue("P_ENABLED" + s))
                if (ep->load() <= 0.5f) continue;

            int animMode = 0;
            int animCurve = 0;
            float animDepth = 1.0f;
            if (auto* p = processor.apvts.getRawParameterValue("ANIM_MODE" + s))  animMode  = (int)p->load();
            if (auto* p = processor.apvts.getRawParameterValue("ANIM_CURVE" + s)) animCurve = (int)p->load();
            if (auto* p = processor.apvts.getRawParameterValue("ANIM_DEPTH" + s)) animDepth = p->load();

            PartialAnimationData track = processor.getPartialAnimation(i);
            if (track.numKeyframes <= 0 || animMode <= 0)
                continue;

            auto col = getPartialColour(i);
            bool isSel = (i == selectedPartial);
            const PartialKeyframe& startKf = track.keyframes[0];
            float effDepth = (animMode > 0) ? animDepth : 1.0f;

            juce::Path motionPath;
            const int pathSteps = 36;
            for (int step = 0; step <= pathSteps; ++step)
            {
                float t = (float)step / (float)pathSteps;
                PartialKeyframe eval = track.evaluate(t, animCurve, startKf);
                float r  = juce::jlimit(0.01f, 32.0f,  startKf.ratio    + effDepth * (eval.ratio    - startKf.ratio));
                float gn = juce::jlimit(0.0f,  1.0f,   startKf.gain     + effDepth * (eval.gain     - startKf.gain));
                float sm = juce::jlimit(-24.0f, 24.0f, startKf.semitone + effDepth * (eval.semitone - startKf.semitone));
                float effR = juce::jlimit(0.01f, 32.0f, r * std::pow(2.0f, sm / 12.0f));
                auto pt = mapParamsToScreen(effR, gn, sm, 0.0f, left, top, width, height, baseFreq);
                if (step == 0) motionPath.startNewSubPath(pt);
                else           motionPath.lineTo(pt);
            }

            g.setColour(col.withAlpha(isSel ? 0.30f : 0.14f));
            g.strokePath(motionPath, juce::PathStrokeType(isSel ? 3.5f : 2.0f));
            g.setColour(col.withAlpha(isSel ? 0.85f : 0.45f));
            g.strokePath(motionPath, juce::PathStrokeType(isSel ? 1.5f : 1.0f));

            // Draw waypoint diamonds along the path
            for (int k = 0; k < track.numKeyframes; ++k)
            {
                const auto& kf = track.keyframes[k];
                float r  = juce::jlimit(0.01f, 32.0f,  startKf.ratio    + effDepth * (kf.ratio    - startKf.ratio));
                float gn = juce::jlimit(0.0f,  1.0f,   startKf.gain     + effDepth * (kf.gain     - startKf.gain));
                float sm = juce::jlimit(-24.0f, 24.0f, startKf.semitone + effDepth * (kf.semitone - startKf.semitone));
                float effR = juce::jlimit(0.01f, 32.0f, r * std::pow(2.0f, sm / 12.0f));
                auto pt = mapParamsToScreen(effR, gn, sm, 0.0f, left, top, width, height, baseFreq);

                float dr = (isSel && k == selectedKeyframeIndex) ? 5.0f : (isSel ? 3.8f : 2.8f);
                juce::Path dm;
                dm.startNewSubPath(pt.x, pt.y - dr);
                dm.lineTo(pt.x + dr, pt.y);
                dm.lineTo(pt.x, pt.y + dr);
                dm.lineTo(pt.x - dr, pt.y);
                dm.closeSubPath();
                g.setColour(isSel ? juce::Colours::white : col.withAlpha(0.75f));
                g.fillPath(dm);
                g.setColour(col);
                g.strokePath(dm, juce::PathStrokeType(1.0f));
            }
        }

        // 2. Overlapping Harmonic Bell Curves (follow live modulated positions!)
        for (int i = 0; i < numActive; ++i)
        {
            auto idSuffix = juce::String(i + 1);
            if (auto* ep = processor.apvts.getRawParameterValue("P_ENABLED" + idSuffix))
                if (ep->load() <= 0.5f) continue;

            float px = coords[i].livePos.x;
            float py = coords[i].livePos.y;
            float modGain = coords[i].modGain;
            if (modGain <= 0.005f) continue;

            bool isSelected = (i == selectedPartial);
            bool isHovered  = (i == hoveredPartial);
            bool isDragged  = (i == draggedPartial);
            auto col = getPartialColour(i);

            float peakH = (top + height) - py;
            if (peakH <= 1.0f) continue;

            float halfWidth = juce::jlimit(16.0f, 44.0f, width * 0.05f);
            float xStart = juce::jmax(left, px - halfWidth);
            float xEnd   = juce::jmin(left + width, px + halfWidth);

            juce::Path bellPath;
            bellPath.startNewSubPath(xStart, top + height);
            int steps = 24;
            for (int s = 0; s <= steps; ++s)
            {
                float curX = xStart + (xEnd - xStart) * ((float)s / (float)steps);
                float distNorm = (curX - px) / halfWidth;
                if (distNorm >= -1.0f && distNorm <= 1.0f)
                {
                    float bell = 0.5f * (1.0f + std::cos(distNorm * juce::MathConstants<float>::pi));
                    float curY = (top + height) - peakH * bell;
                    bellPath.lineTo(curX, curY);
                }
            }
            bellPath.lineTo(xEnd, top + height);
            bellPath.closeSubPath();

            float alphaFill = (isSelected || isDragged) ? 0.30f : (isHovered ? 0.22f : 0.12f);
            g.setColour(col.withAlpha(alphaFill));
            g.fillPath(bellPath);

            float alphaLine = (isSelected || isDragged) ? 0.95f : (isHovered ? 0.80f : 0.45f);
            float lineW = (isSelected || isDragged) ? 1.8f : 1.0f;
            g.setColour(col.withAlpha(alphaLine));
            g.strokePath(bellPath, juce::PathStrokeType(lineW));
        }

        // 3. Interactive Harmonic Nodes (Pucks, Stems, Anchor Ghost Rings & Pan Indicators)
        // Draw selectedPartial and draggedPartial last so the active node is always on top when overlapping
        std::array<int, 16> drawOrder;
        int orderCount = 0;
        for (int i = 0; i < numActive; ++i)
        {
            if (i != selectedPartial && i != draggedPartial)
                drawOrder[orderCount++] = i;
        }
        if (selectedPartial >= 0 && selectedPartial < numActive && selectedPartial != draggedPartial)
            drawOrder[orderCount++] = selectedPartial;
        if (draggedPartial >= 0 && draggedPartial < numActive)
            drawOrder[orderCount++] = draggedPartial;

        for (int idx = 0; idx < orderCount; ++idx)
        {
            int i = drawOrder[idx];
            auto idSuffix = juce::String(i + 1);
            if (auto* ep = processor.apvts.getRawParameterValue("P_ENABLED" + idSuffix))
                if (ep->load() <= 0.5f) continue;

            float px = coords[i].livePos.x;
            float py = coords[i].livePos.y;
            float ax = coords[i].anchorPos.x;
            float ay = coords[i].anchorPos.y;

            bool isSelected = (i == selectedPartial);
            bool isHovered  = (i == hoveredPartial);
            bool isDragged  = (i == draggedPartial);
            auto col = getPartialColour(i);

            // If ADSR/LFO/Animation is displacing livePos from anchorPos, draw ghost anchor + tether
            float dispDist = std::hypot(px - ax, py - ay);
            if (dispDist > 2.5f)
            {
                g.setColour(col.withAlpha(0.38f));
                g.drawLine(ax, ay, px, py, 1.2f);
                g.drawEllipse(ax - 5.0f, ay - 5.0f, 10.0f, 10.0f, 1.1f);
            }

            // Stem line
            float stemBaseY = top + height;
            if (isDragged || isHovered)
                g.setColour(col.brighter(0.4f));
            else if (isSelected)
                g.setColour(col);
            else
                g.setColour(col.withAlpha(0.6f));

            float strokeW = (isSelected || isHovered || isDragged) ? 2.0f : 1.2f;
            g.drawLine(px, stemBaseY, px, py, strokeW);

            // Base pin dot
            g.setColour(col.withAlpha(0.8f));
            g.fillEllipse(px - 2.5f, stemBaseY - 2.5f, 5.0f, 5.0f);

            // Puck
            float radius = (isDragged || isHovered) ? 11.0f : (isSelected ? 10.0f : 8.5f);

            // Outer glow
            if (isDragged || isHovered)
            {
                g.setColour(col.withAlpha(0.6f));
                g.fillEllipse(px - radius - 3.0f, py - radius - 3.0f, (radius + 3.0f) * 2.0f, (radius + 3.0f) * 2.0f);
            }
            else if (isSelected)
            {
                g.setColour(col.withAlpha(0.4f));
                g.fillEllipse(px - radius - 3.0f, py - radius - 3.0f, (radius + 3.0f) * 2.0f, (radius + 3.0f) * 2.0f);
            }

            // Stereo Pan modulation crescent indicator around puck
            if (std::abs(coords[i].modPan) > 0.03f)
            {
                float panVal = coords[i].modPan;
                juce::Path panArc;
                float centerAngle = (panVal > 0.0f) ? (juce::MathConstants<float>::halfPi) : (-juce::MathConstants<float>::halfPi);
                float span = std::abs(panVal) * (juce::MathConstants<float>::pi * 0.45f);
                panArc.addCentredArc(px, py, radius + 3.5f, radius + 3.5f, 0.0f, centerAngle - span, centerAngle + span, true);
                g.setColour(juce::Colours::white.withAlpha(0.85f));
                g.strokePath(panArc, juce::PathStrokeType(2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }

            // Puck fill
            if (isDragged || isHovered)
                g.setColour(col);
            else if (isSelected)
                g.setColour(col.withMultipliedSaturation(0.8f));
            else
                g.setColour(juce::Colour(0xFF151515));
            g.fillEllipse(px - radius, py - radius, radius * 2.0f, radius * 2.0f);

            // Puck ring
            if (isDragged || isHovered)
                g.setColour(juce::Colours::white);
            else if (isSelected)
                g.setColour(col.brighter(0.6f));
            else
                g.setColour(col.withAlpha(0.85f));
            g.drawEllipse(px - radius, py - radius, radius * 2.0f, radius * 2.0f, 1.6f);

            // Puck label (harmonic number)
            if (isDragged || isHovered || isSelected)
                g.setColour(juce::Colours::black);
            else
                g.setColour(juce::Colours::white);

            g.setFont(juce::Font(radius > 9.0f ? 11.0f : 9.5f, juce::Font::bold));
            g.drawText(juce::String(i + 1), (int)(px - radius), (int)(py - radius), (int)(radius * 2.0f), (int)(radius * 2.0f), juce::Justification::centred);
        }

        // 4. Floating HUD badge for active/hovered/dragged partial
        if (activeTarget >= 0 && activeTarget < numActive)
        {
            auto idSuffix = juce::String(activeTarget + 1);
            const auto& c = coords[activeTarget];
            float px = c.livePos.x;
            float py = c.livePos.y;

            float harmFreq = c.modRatio * baseFreq;
            juce::String fStr = (harmFreq >= 1000.0f)
                ? (juce::String(harmFreq / 1000.0f, 1) + " kHz")
                : (juce::String((int)std::round(harmFreq)) + " Hz");

            juce::String hudText = "H" + idSuffix + ": " + juce::String(c.modRatio, 2) + "x (" + fStr + ")"
                                 + " | Gain " + juce::String((int)std::round(c.modGain * 100.0f)) + "%";
            if (std::abs(c.modSemi) >= 0.05f)
                hudText += " | " + juce::String(c.modSemi > 0 ? "+" : "") + juce::String((int)std::round(c.modSemi)) + "st";

            if (draggedPartial >= 0)
                hudText += (isDraggingSemitone ? " [SEMI]" : " [EDIT]");

            int hudWidth = 248;
            int hudHeight = 20;
            float hudRadius = (activeTarget == draggedPartial || activeTarget == hoveredPartial) ? 11.0f : 10.0f;
            float hudX = juce::jlimit(left, left + width - hudWidth, px - hudWidth * 0.5f);
            float hudY = py - hudRadius - 24.0f;
            if (hudY < top) hudY = py + 14.0f;

            g.setColour(juce::Colour(0xEE121222));
            g.fillRoundedRectangle(hudX, hudY, (float)hudWidth, (float)hudHeight, 4.0f);
            g.setColour(juce::Colour(0xFFFFFFFF));
            g.drawRoundedRectangle(hudX, hudY, (float)hudWidth, (float)hudHeight, 4.0f, 1.0f);

            g.setColour(juce::Colours::white);
            g.setFont(10.0f);
            g.drawText(hudText, (int)hudX, (int)hudY, hudWidth, hudHeight, juce::Justification::centred);
        }
    } // End of if (!isReadOnly)

    // Interactive Keyframe Timeline Dock (only drawn here if no external bottom-of-screen host is attached)
    if (isTimelineOpen && timelineHost == nullptr)
    {
        juce::Rectangle<float> tlArea(inner.getX(), inner.getBottom() - 64.0f, inner.getWidth(), 64.0f);
        paintKeyframeTimeline(g, tlArea);
    }

    // Top Title & Quick Guide (constrain width to avoid overlapping top-right buttons)
    int buttonsLeft = slideBtn.isVisible() ? (slideBtn.getX() - 8) : (showNodesBtn.getX() - 8);
    int maxTitleW = juce::jmax(80, buttonsLeft - (int)left);

    bool isFull = (scaleMode == SpectrumScaleMode::Full);
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(11.0f, juce::Font::bold));

    juce::String titleStr;
    if (isWaterfallMode)
        titleStr = isFull ? "WATERFALL (20 Hz - 20 kHz)" : ("WATERFALL (Base: " + juce::String((int)std::round(baseFreq)) + " Hz)");
    else if (isFull)
        titleStr = "FULL SPECTRUM (20 Hz - 20 kHz)";
    else
        titleStr = "SLIDING SPECTRUM (Base: " + juce::String((int)std::round(baseFreq)) + " Hz)";

    if (isLocked)
        titleStr += " [LOCKED]";

    g.drawText(titleStr, (int)left, 4, maxTitleW, 14, juce::Justification::centredLeft);

    g.setColour(juce::Colours::white.withAlpha(0.45f));
    g.setFont(9.0f);
    juce::String subStr;
    if (isWaterfallMode)
    {
        juce::String palName = "Spectrum";
        switch (currentPalette)
        {
            case WaterfallPalette::Spectrum:   palName = "Spectrum";   break;
            case WaterfallPalette::Aurora:     palName = "Aurora";     break;
            case WaterfallPalette::Thermal:    palName = "Thermal";    break;
            case WaterfallPalette::Ocean:      palName = "Ocean";      break;
            case WaterfallPalette::Monochrome: palName = "Monochrome"; break;
            default: break;
        }

        subStr = isFull
            ? (isLocked ? ("Waterfall [" + palName + "] | Fixed 20Hz - 20kHz | Nodes locked")
                        : ("Waterfall [" + palName + "] | Fixed 20Hz - 20kHz | Downward scroll | Click right side bar to switch themes"))
            : (isLocked ? ("Waterfall [" + palName + "] | Sliding scale | Nodes locked")
                        : ("Waterfall [" + palName + "] | Sliding harmonic scale | Downward scroll | Click right side bar to switch themes"));
    }
    else
        subStr = isFull
            ? (isLocked ? "Fixed 20Hz - 20kHz scale | Nodes locked (keyframing active)"
                        : "Fixed 20Hz - 20kHz scale | Drag: Ratio & Gain - Alt: Snap - Ctrl: Semi")
            : (isLocked ? "Sliding harmonic scale | Nodes locked (keyframing active)"
                        : "Sliding harmonic scale | Drag: Ratio & Gain - Alt: Snap - Ctrl: Semi");
    g.drawText(subStr, (int)left, 18, maxTitleW, 12, juce::Justification::centredLeft);

    // Spectrograph Cursor HUD (Frequency & Note Readout)
    if (isMouseHovering && !isDraggingSemitone && draggedPartial < 0)
    {
        auto plotBounds = getGraphPlotBounds();
        if (plotBounds.contains(mouseHoverPos))
        {
            float pLeft = plotBounds.getX();
            float pTop = plotBounds.getY();
            float pWidth = plotBounds.getWidth();
            float pHeight = plotBounds.getHeight();

            float freq = 0.0f;
            float bFreq = getEffectiveBaseFreq();

            if (scaleMode == SpectrumScaleMode::Full)
            {
                float normStatic = juce::jlimit(0.0f, 1.0f, (mouseHoverPos.x - pLeft) / pWidth);
                freq = 20.0f * std::pow(20000.0f / 20.0f, normStatic);
            }
            else
            {
                float normX = juce::jlimit(0.0f, 1.0f, (mouseHoverPos.x - pLeft) / pWidth);
                if (auto* rp = processor.apvts.getParameter("RATIO1"))
                {
                    float r = rp->convertFrom0to1(normX);
                    freq = bFreq * r;
                }
                else
                {
                    freq = bFreq * (0.01f + std::pow(normX, 2.5f) * 31.99f);
                }
            }

            float normY = juce::jlimit(0.0f, 1.0f, ((pTop + pHeight) - mouseHoverPos.y) / pHeight);
            float db = (normY > 0.0001f) ? juce::Decibels::gainToDecibels(normY) : -96.0f;

            float midiFloat = 69.0f + 12.0f * std::log2(juce::jmax(8.0f, freq) / 440.0f);
            int midiNote = juce::jlimit(0, 127, (int)std::round(midiFloat));
            juce::String noteName = juce::MidiMessage::getMidiNoteName(midiNote, true, true, 4);

            juce::String hudText = juce::String(freq, 1) + " Hz (" + noteName + ") | " + juce::String(db, 1) + " dB";

            int textW = 165;
            int textH = 20;
            float hx = juce::jlimit(pLeft + 4.0f, pLeft + pWidth - textW - 4.0f, mouseHoverPos.x + 12.0f);
            float hy = juce::jlimit(pTop + 4.0f, pTop + pHeight - textH - 4.0f, mouseHoverPos.y - 24.0f);

            g.setColour(juce::Colour(0xEE121212));
            g.fillRoundedRectangle(hx, hy, (float)textW, (float)textH, 4.0f);
            g.setColour(juce::Colour(0x66FFFFFF));
            g.drawRoundedRectangle(hx, hy, (float)textW, (float)textH, 4.0f, 1.0f);

            g.setColour(juce::Colours::white);
            g.setFont(juce::Font(10.5f, juce::Font::bold));
            g.drawText(hudText, (int)hx, (int)hy, textW, textH, juce::Justification::centred);
        }
    }
}

int Spectrograph::findPartialAt(float mouseX, float mouseY)
{
    auto plotBounds = getGraphPlotBounds();
    float left   = plotBounds.getX();
    float top    = plotBounds.getY();
    float width  = plotBounds.getWidth();
    float height = plotBounds.getHeight();

    if (mouseY > top + height + 14.0f)
        return -1;

    int numActive = (int)processor.apvts.getRawParameterValue("NUM_PARTIALS")->load();
    numActive = juce::jlimit(1, 16, numActive);

    float baseFreq = getEffectiveBaseFreq();

    auto isPartialPuckHit = [&](int i) -> bool
    {
        if (i < 0 || i >= numActive) return false;
        auto idSuffix = juce::String(i + 1);
        if (auto* ep = processor.apvts.getRawParameterValue("P_ENABLED" + idSuffix))
            if (ep->load() <= 0.5f) return false;

        auto c = computePartialCoords(i, left, top, width, height, baseFreq);
        return (std::hypot(mouseX - c.livePos.x, mouseY - c.livePos.y) <= 16.0f ||
                std::hypot(mouseX - c.anchorPos.x, mouseY - c.anchorPos.y) <= 16.0f);
    };

    // Prioritize selectedPartial first so overlapping nodes (e.g. at 0st, 0ct in Pitch/Detune mode) grab the selected partial
    if (isPartialPuckHit(selectedPartial))
        return selectedPartial;

    // Search pucks in reverse order (check both live modulated pos and anchor pos)
    for (int i = numActive - 1; i >= 0; --i)
    {
        if (i == selectedPartial) continue;
        if (isPartialPuckHit(i))
            return i;
    }

    // Search stems (prioritize selectedPartial first)
    float stemBaseY = top + height;
    auto isPartialStemHit = [&](int i) -> bool
    {
        if (i < 0 || i >= numActive) return false;
        auto idSuffix = juce::String(i + 1);
        if (auto* ep = processor.apvts.getRawParameterValue("P_ENABLED" + idSuffix))
            if (ep->load() <= 0.5f) return false;

        auto c = computePartialCoords(i, left, top, width, height, baseFreq);
        float minY = std::min(c.livePos.y, stemBaseY) - 4.0f;
        float maxY = std::max(c.livePos.y, stemBaseY) + 4.0f;
        return (std::abs(mouseX - c.livePos.x) <= 8.0f && mouseY >= minY && mouseY <= maxY);
    };

    if (isPartialStemHit(selectedPartial))
        return selectedPartial;

    for (int i = numActive - 1; i >= 0; --i)
    {
        if (i == selectedPartial) continue;
        if (isPartialStemHit(i))
            return i;
    }

    return -1;
}

int Spectrograph::findClosestPartialX(float mouseX)
{
    auto plotBounds = getGraphPlotBounds();
    float left   = plotBounds.getX();
    float top    = plotBounds.getY();
    float width  = plotBounds.getWidth();
    float height = plotBounds.getHeight();

    int numActive = (int)processor.apvts.getRawParameterValue("NUM_PARTIALS")->load();
    numActive = juce::jlimit(1, 16, numActive);

    float baseFreq = getEffectiveBaseFreq();

    int closest = -1;
    float minDist = 1e9f;
    for (int i = 0; i < numActive; ++i)
    {
        auto c = computePartialCoords(i, left, top, width, height, baseFreq);
        float d = std::abs(mouseX - c.livePos.x);
        if (d < minDist)
        {
            minDist = d;
            closest = i;
        }
    }
    return closest;
}

void Spectrograph::handleTimelineMouseMove(const juce::MouseEvent& e)
{
    if (timelineHost != nullptr)
    {
        if (getTimelineTrackBounds().expanded(4.0f, 4.0f).contains(e.position))
            timelineHost->setMouseCursor(juce::MouseCursor::PointingHandCursor);
        else
            timelineHost->setMouseCursor(juce::MouseCursor::NormalCursor);
    }
}

void Spectrograph::handleTimelineMouseDown(const juce::MouseEvent& e)
{
    if (selectedPartial < 0) return;
    auto trackRect = getTimelineTrackBounds();
    if (trackRect.expanded(6.0f, 6.0f).contains(e.position))
    {
        int kfHit = findKeyframeAtTimelinePos(e.position.x, e.position.y, trackRect);
        if (e.mods.isRightButtonDown())
        {
            if (kfHit >= 0)
            {
                processor.removeKeyframe(selectedPartial, kfHit);
                selectedKeyframeIndex = -1;
                repaint();
                repaintTimeline();
            }
            return;
        }

        if (kfHit >= 0)
        {
            selectedKeyframeIndex = kfHit;
            draggedKeyframeIndex = kfHit;
            PartialAnimationData track = processor.getPartialAnimation(selectedPartial);
            if (kfHit < track.numKeyframes)
                scrubTimeNorm = track.keyframes[kfHit].time;
            syncSlidersToPlayhead(selectedPartial);
            repaint();
            repaintTimeline();
            return;
        }

        // Scrub playhead along timeline
        isScrubbingTimeline = true;
        selectedKeyframeIndex = -1;
        scrubTimeNorm = juce::jlimit(0.0f, 1.0f, (e.position.x - trackRect.getX()) / trackRect.getWidth());
        syncSlidersToPlayhead(selectedPartial);
        repaint();
        repaintTimeline();
    }
}

void Spectrograph::handleTimelineMouseDrag(const juce::MouseEvent& e)
{
    if (draggedKeyframeIndex >= 0 || isScrubbingTimeline)
    {
        auto trackRect = getTimelineTrackBounds();
        float newT = juce::jlimit(0.0f, 1.0f, (e.position.x - trackRect.getX()) / trackRect.getWidth());
        scrubTimeNorm = newT;

        if (draggedKeyframeIndex >= 0)
        {
            draggedKeyframeIndex = processor.moveKeyframeTime(selectedPartial, draggedKeyframeIndex, newT);
            selectedKeyframeIndex = draggedKeyframeIndex;
        }
        syncSlidersToPlayhead(selectedPartial);
        repaint();
        repaintTimeline();
    }
}

void Spectrograph::handleTimelineMouseUp(const juce::MouseEvent&)
{
    isScrubbingTimeline = false;
    draggedKeyframeIndex = -1;
    repaint();
    repaintTimeline();
}

void Spectrograph::handleTimelineMouseDoubleClick(const juce::MouseEvent& e)
{
    auto trackRect = getTimelineTrackBounds();
    if (trackRect.expanded(4.0f, 4.0f).contains(e.position))
    {
        scrubTimeNorm = juce::jlimit(0.0f, 1.0f, (e.position.x - trackRect.getX()) / trackRect.getWidth());
        addKfBtn.triggerClick();
    }
}

void Spectrograph::mouseMove(const juce::MouseEvent& e)
{
    mouseHoverPos = e.position;
    isMouseHovering = true;

    if (isWaterfallMode)
    {
        auto plotBounds = getGraphPlotBounds();
        float left   = plotBounds.getX();
        float top    = plotBounds.getY();
        float width  = plotBounds.getWidth();
        float height = plotBounds.getHeight();
        int legendW = 10;
        int legendX = (int)(left + width) - legendW - 2;

        if (e.position.x >= (float)(legendX - 4) && e.position.x <= (float)(legendX + legendW + 4) &&
            e.position.y >= top && e.position.y <= top + height)
        {
            setMouseCursor(juce::MouseCursor::PointingHandCursor);
            repaint();
            return;
        }
    }

    if (isReadOnly) { repaint(); return; }

    int hit = findPartialAt(e.position.x, e.position.y);
    if (hit != hoveredPartial)
    {
        hoveredPartial = hit;
        if (hoveredPartial >= 0)
            setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        else
            setMouseCursor(juce::MouseCursor::NormalCursor);
    }
    repaint();
}

void Spectrograph::mouseExit(const juce::MouseEvent&)
{
    setMouseCursor(juce::MouseCursor::NormalCursor);
    isMouseHovering = false;
    if (isReadOnly) { repaint(); return; }
    if (hoveredPartial != -1)
    {
        hoveredPartial = -1;
    }
    repaint();
}

void Spectrograph::mouseDown(const juce::MouseEvent& e)
{
    if (isWaterfallMode)
    {
        auto plotBounds = getGraphPlotBounds();
        float left   = plotBounds.getX();
        float top    = plotBounds.getY();
        float width  = plotBounds.getWidth();
        float height = plotBounds.getHeight();
        int legendW = 10;
        int legendX = (int)(left + width) - legendW - 2;

        if (e.position.x >= (float)(legendX - 6) && e.position.x <= (float)(legendX + legendW + 6) &&
            e.position.y >= top && e.position.y <= top + height)
        {
            cycleWaterfallPalette();
            return;
        }
    }

    if (isTimelineOpen && timelineHost == nullptr)
    {
        auto trackRect = getTimelineTrackBounds();
        if (trackRect.expanded(6.0f, 6.0f).contains(e.position))
        {
            handleTimelineMouseDown(e);
            return;
        }
    }

    if (isReadOnly) return;

    int hit = findPartialAt(e.position.x, e.position.y);
    draggedWaypointKeyframe = -1;

    // Check if the user clicked on a keyframe waypoint diamond of the selected partial (or any partial if no puck was hit)
    int diamondPartial = -1;
    int diamondKf = findWaypointDiamondAt(e.position.x, e.position.y, diamondPartial);
    if (diamondKf >= 0 && (hit < 0 || (diamondPartial == selectedPartial && hit != selectedPartial)))
    {
        hit = diamondPartial;
        setSelectedPartial(hit);
        PartialAnimationData dTrack = processor.getPartialAnimation(hit);
        if (diamondKf < dTrack.numKeyframes)
        {
            selectedKeyframeIndex = diamondKf;
            draggedWaypointKeyframe = diamondKf;
            scrubTimeNorm = dTrack.keyframes[diamondKf].time;
            syncSlidersToPlayhead(hit);
        }
    }

    if (hit >= 0)
    {
        draggedPartial = hit;
        setSelectedPartial(hit);
        dragStartMouse = e.position;

        // If user clicked directly on a waypoint diamond of this partial when the puck wasn't already at that position, select that diamond for dragging
        if (diamondKf >= 0 && diamondPartial == hit)
        {
            PartialAnimationData dTrack = processor.getPartialAnimation(hit);
            if (diamondKf < dTrack.numKeyframes)
            {
                auto plotBounds = getGraphPlotBounds();
                float baseFreq = getEffectiveBaseFreq();
                auto c = computePartialCoords(hit, plotBounds.getX(), plotBounds.getY(), plotBounds.getWidth(), plotBounds.getHeight(), baseFreq);
                if (std::hypot(e.position.x - c.livePos.x, e.position.y - c.livePos.y) > 14.0f)
                {
                    selectedKeyframeIndex = diamondKf;
                    draggedWaypointKeyframe = diamondKf;
                    scrubTimeNorm = dTrack.keyframes[diamondKf].time;
                    syncSlidersToPlayhead(hit);
                }
            }
        }

        auto idSuffix = juce::String(hit + 1);
        if (auto* rp = processor.apvts.getParameter("RATIO" + idSuffix))
        {
            rp->beginChangeGesture();
            initialRatioNorm = rp->getValue();
        }
        if (auto* gp = processor.apvts.getParameter("GAIN" + idSuffix))
        {
            gp->beginChangeGesture();
            initialGainNorm = gp->getValue();
        }
        if (auto* sp = processor.apvts.getParameter("SEMITONE" + idSuffix))
        {
            sp->beginChangeGesture();
            initialSemitone = sp->convertFrom0to1(sp->getValue());
        }

        // Capture pre-drag state for keyframe editing
        PartialAnimationData track = processor.getPartialAnimation(hit);
        int curve = 0;
        if (auto* cp = processor.apvts.getRawParameterValue("ANIM_CURVE" + idSuffix))
            curve = (int)cp->load();
        PartialKeyframe baseKf = processor.getPartialBaseSettings(hit);
        const PartialKeyframe& fallbackKf = (track.numKeyframes > 0) ? track.keyframes[0] : baseKf;
        initialDragKeyframe = (track.numKeyframes > 0) ? track.evaluate(scrubTimeNorm, curve, fallbackKf) : baseKf;
        initialSemitone = initialDragKeyframe.semitone;
        isDraggingSemitone = (e.mods.isCommandDown() || e.mods.isCtrlDown());

        if (onPartialSelected)
            onPartialSelected(hit);
        repaint();
        repaintTimeline();
    }
    else
    {
        // Clicked on empty space of the graph: deselect partial
        setSelectedPartial(-1);
        if (onPartialSelected)
            onPartialSelected(-1);
    }
}

void Spectrograph::mouseDrag(const juce::MouseEvent& e)
{
    if (isTimelineOpen && timelineHost == nullptr && (draggedKeyframeIndex >= 0 || isScrubbingTimeline))
    {
        handleTimelineMouseDrag(e);
        return;
    }

    if (isReadOnly || draggedPartial < 0) return;

    auto plotBounds = getGraphPlotBounds();
    float left   = plotBounds.getX();
    float top    = plotBounds.getY();
    float width  = plotBounds.getWidth();
    float height = plotBounds.getHeight();

    auto idSuffix = juce::String(draggedPartial + 1);
    PartialAnimationData track = processor.getPartialAnimation(draggedPartial);
    int animMode = 0;
    int animCurve = 0;
    if (auto* p = processor.apvts.getRawParameterValue("ANIM_MODE" + idSuffix))  animMode  = (int)p->load();
    if (auto* p = processor.apvts.getRawParameterValue("ANIM_CURVE" + idSuffix)) animCurve = (int)p->load();

    PartialKeyframe baseKf = processor.getPartialBaseSettings(draggedPartial);
    const PartialKeyframe& fallbackKf = (track.numKeyframes > 0) ? track.keyframes[0] : baseKf;
    PartialKeyframe prevEval = (animMode > 0 && track.numKeyframes > 0) ? track.evaluate(scrubTimeNorm, animCurve, fallbackKf) : initialDragKeyframe;
    PartialKeyframe currentKf = prevEval;
    currentKf.time = scrubTimeNorm;

    // Keyframe at playhead when timeline is open and paused; otherwise shift existing keyframes (or update dragged waypoint diamond)
    bool shouldUpdateKfAtPlayhead = (animMode > 0) && isTimelineOpen;

    isDraggingSemitone = (e.mods.isCommandDown() || e.mods.isCtrlDown());
    if (isDraggingSemitone)
    {
        // Ctrl/Cmd: Semitone offset
        float deltaY = dragStartMouse.y - e.position.y;
        float newSemi = juce::jlimit(-24.0f, 24.0f, initialSemitone + std::round(deltaY / 6.0f));
        currentKf.semitone = newSemi;
        if (auto* sp = processor.apvts.getParameter("SEMITONE" + idSuffix))
            sp->setValueNotifyingHost(sp->convertTo0to1(newSemi));
    }
    else
    {
        // X = Ratio, Y = Gain
        float normX = 0.0f;
        float targetEffRatio = 1.0f;
        auto* rp = processor.apvts.getParameter("RATIO" + idSuffix);
        float baseFreq = getEffectiveBaseFreq();

        if (scaleMode == SpectrumScaleMode::Full)
        {
            float normStatic = juce::jlimit(0.0f, 1.0f, (e.position.x - left) / width);
            float harmFreq = 20.0f * std::pow(20000.0f / 20.0f, normStatic);
            targetEffRatio = harmFreq / baseFreq;
        }
        else
        {
            normX = juce::jlimit(0.0f, 1.0f, (e.position.x - left) / width);
            if (rp != nullptr)
                targetEffRatio = rp->convertFrom0to1(normX);
        }

        // Compensate for active semitone shift so the node stays directly under cursor
        float pitchShiftFactor = std::pow(2.0f, currentKf.semitone / 12.0f);
        float newRatio = (pitchShiftFactor > 0.0001f) ? (targetEffRatio / pitchShiftFactor) : targetEffRatio;

        if (e.mods.isAltDown())
        {
            newRatio = juce::jlimit(0.1f, 16.0f, std::round(newRatio));
        }
        newRatio = juce::jlimit(0.01f, 32.0f, newRatio);

        float newGain = juce::jlimit(0.0f, 1.0f, ((top + height) - e.position.y) / height);

        currentKf.ratio = newRatio;
        currentKf.gain  = newGain;

        if (rp != nullptr)
            rp->setValueNotifyingHost(rp->convertTo0to1(newRatio));
        if (auto* gp = processor.apvts.getParameter("GAIN" + idSuffix))
            gp->setValueNotifyingHost(gp->convertTo0to1(newGain));
    }

    if (shouldUpdateKfAtPlayhead)
    {
        if (track.numKeyframes == 0 && scrubTimeNorm > 0.015f)
        {
            PartialKeyframe startKf = initialDragKeyframe;
            startKf.time = 0.0f;
            processor.addOrUpdateKeyframe(draggedPartial, startKf, 0.01f);
        }
        selectedKeyframeIndex = processor.addOrUpdateKeyframe(draggedPartial, currentKf, 0.015f);
        PartialAnimationData updated = processor.getPartialAnimation(draggedPartial);
        if (selectedKeyframeIndex >= 0 && selectedKeyframeIndex < updated.numKeyframes)
            scrubTimeNorm = updated.keyframes[selectedKeyframeIndex].time;

        if (auto* adp = processor.apvts.getParameter("ANIM_DEPTH" + idSuffix))
        {
            if (adp->convertFrom0to1(adp->getValue()) < 0.99f)
            {
                adp->beginChangeGesture();
                adp->setValueNotifyingHost(adp->convertTo0to1(1.0f));
                adp->endChangeGesture();
            }
        }
    }
    else if (animMode > 0 && draggedWaypointKeyframe >= 0 && draggedWaypointKeyframe < track.numKeyframes)
    {
        // Directly reshape the grabbed waypoint diamond on the graph without pausing playback
        track.keyframes[draggedWaypointKeyframe].ratio    = currentKf.ratio;
        track.keyframes[draggedWaypointKeyframe].gain     = currentKf.gain;
        track.keyframes[draggedWaypointKeyframe].semitone = currentKf.semitone;
        track.keyframes[draggedWaypointKeyframe].detune   = 0.0f;
        processor.setPartialAnimation(draggedPartial, track);
    }
    else if (animMode > 0 && track.numKeyframes > 0)
    {
        // Shift the keyframe curve by the drag delta so the node and its path move smoothly
        float dRatio  = currentKf.ratio    - prevEval.ratio;
        float dGain   = currentKf.gain     - prevEval.gain;
        float dSemi   = currentKf.semitone - prevEval.semitone;
        for (int k = 0; k < track.numKeyframes; ++k)
        {
            track.keyframes[k].ratio    = juce::jlimit(0.01f, 32.0f,  track.keyframes[k].ratio    + dRatio);
            track.keyframes[k].gain     = juce::jlimit(0.0f,  1.0f,   track.keyframes[k].gain     + dGain);
            track.keyframes[k].semitone = juce::jlimit(-24.0f, 24.0f, track.keyframes[k].semitone + dSemi);
            track.keyframes[k].detune   = 0.0f;
        }
        processor.setPartialAnimation(draggedPartial, track);
    }

    float effRatio = juce::jlimit(0.01f, 32.0f, currentKf.ratio * std::pow(2.0f, currentKf.semitone / 12.0f));
    smoothedModRatio[draggedPartial]  = effRatio;
    smoothedModGain[draggedPartial]   = currentKf.gain;
    smoothedModSemi[draggedPartial]   = currentKf.semitone;
    smoothedModDetune[draggedPartial] = 0.0f;
    smoothedModPan[draggedPartial]    = currentKf.pan;

    repaint();
    repaintTimeline();
}

void Spectrograph::mouseUp(const juce::MouseEvent&)
{
    isScrubbingTimeline = false;
    draggedKeyframeIndex = -1;
    draggedWaypointKeyframe = -1;
    isDraggingSemitone = false;

    if (isReadOnly) return;
    if (draggedPartial >= 0)
    {
        auto idSuffix = juce::String(draggedPartial + 1);
        if (auto* rp = processor.apvts.getParameter("RATIO" + idSuffix))
            rp->endChangeGesture();
        if (auto* gp = processor.apvts.getParameter("GAIN" + idSuffix))
            gp->endChangeGesture();
        if (auto* sp = processor.apvts.getParameter("SEMITONE" + idSuffix))
            sp->endChangeGesture();

        draggedPartial = -1;
        repaint();
        repaintTimeline();
    }
}

void Spectrograph::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (isTimelineOpen && timelineHost == nullptr)
    {
        auto trackRect = getTimelineTrackBounds();
        if (trackRect.expanded(4.0f, 4.0f).contains(e.position))
        {
            handleTimelineMouseDoubleClick(e);
            return;
        }
    }

    if (isReadOnly) return;
    int hit = findPartialAt(e.position.x, e.position.y);
    if (hit >= 0)
    {
        auto idSuffix = juce::String(hit + 1);
        if (auto* rp = processor.apvts.getParameter("RATIO" + idSuffix))
        {
            rp->beginChangeGesture();
            rp->setValueNotifyingHost(rp->convertTo0to1((float)(hit + 1)));
            rp->endChangeGesture();
        }

        if (auto* gp = processor.apvts.getParameter("GAIN" + idSuffix))
        {
            gp->beginChangeGesture();
            gp->setValueNotifyingHost(gp->convertTo0to1(hit <= 3 ? 1.0f / (float)(hit + 1) : 0.0f));
            gp->endChangeGesture();
        }

        if (auto* dp = processor.apvts.getParameter("DETUNE" + idSuffix))
        {
            dp->beginChangeGesture();
            dp->setValueNotifyingHost(dp->convertTo0to1(0.0f));
            dp->endChangeGesture();
        }

        if (auto* sp = processor.apvts.getParameter("SEMITONE" + idSuffix))
        {
            sp->beginChangeGesture();
            sp->setValueNotifyingHost(sp->convertTo0to1(0.0f));
            sp->endChangeGesture();
        }

        recordKeyframeFromSliders(hit);
        repaint();
        repaintTimeline();
    }
}

void Spectrograph::timerCallback()
{
    if (!isShowing() || isFrozen)
        return;

    juce::uint32 nowMs = juce::Time::getMillisecondCounter();
    float dt = (lastTimerMs > 0) ? juce::jlimit(0.001f, 0.1f, (float)(nowMs - lastTimerMs) * 0.001f) : 0.02f;
    lastTimerMs = nowMs;

    int writePos = processor.ringBufferWritePos.load(std::memory_order_acquire);
    int startPos = (writePos - fftSize + processor.ringBufferSize) % processor.ringBufferSize;
    
    for (int i = 0; i < fftSize; ++i)
    {
        int pos = (startPos + i) % processor.ringBufferSize;
        fftData[i] = processor.ringBuffer[pos];
    }
    for (int i = fftSize; i < 2 * fftSize; ++i)
    {
        fftData[i] = 0.0f;
    }

    // Update live modulation states (ADSR + LFO + Keyframes) for all 16 partials
    bool voiceActive = processor.anyVoiceActiveVis.load(std::memory_order_relaxed);
    float masterPan = 0.0f;
    if (auto* mp = processor.apvts.getRawParameterValue("MASTER_PAN"))
        masterPan = mp->load();

    for (int i = 0; i < 16; ++i)
    {
        juce::String s = juce::String(i + 1);
        float baseRatio  = 1.0f;
        float baseGain   = 0.0f;
        float baseSemi   = 0.0f;
        float basePan    = 0.0f;
        if (auto* p = processor.apvts.getRawParameterValue("RATIO" + s))    baseRatio  = p->load();
        if (auto* p = processor.apvts.getRawParameterValue("GAIN" + s))     baseGain   = p->load();
        if (auto* p = processor.apvts.getRawParameterValue("SEMITONE" + s)) baseSemi   = p->load();
        if (auto* p = processor.apvts.getRawParameterValue("PAN" + s))      basePan    = p->load();

        int animMode = 0, animCurve = 0;
        float animDur = 2.0f, animDepth = 1.0f;
        if (auto* p = processor.apvts.getRawParameterValue("ANIM_MODE" + s))     animMode  = (int)p->load();
        if (auto* p = processor.apvts.getRawParameterValue("ANIM_CURVE" + s))    animCurve = (int)p->load();
        if (auto* p = processor.apvts.getRawParameterValue("ANIM_DURATION" + s)) animDur   = juce::jmax(0.05f, p->load());
        if (auto* p = processor.apvts.getRawParameterValue("ANIM_DEPTH" + s))    animDepth = p->load();

        int modTarget = 0, modWave = 0;
        float modRate = 2.0f, modDepth = 0.0f, modPhase = 0.0f;
        if (auto* p = processor.apvts.getRawParameterValue("MOD_TARGET" + s))   modTarget = (int)p->load();
        if (auto* p = processor.apvts.getRawParameterValue("MOD_WAVEFORM" + s)) modWave   = (int)p->load();
        if (auto* p = processor.apvts.getRawParameterValue("MOD_RATE" + s))     modRate   = p->load();
        if (auto* p = processor.apvts.getRawParameterValue("MOD_DEPTH" + s))    modDepth  = p->load();
        if (auto* p = processor.apvts.getRawParameterValue("MOD_PHASE" + s))    modPhase  = p->load();

        float targetRatio  = baseRatio;
        float targetGain   = baseGain;
        float targetSemi   = baseSemi;
        float targetPan    = basePan;

        if (voiceActive && draggedPartial != i)
        {
            // Live modulation directly from active synth voice
            targetRatio  = processor.livePartialState[i].effectiveRatio.load(std::memory_order_relaxed);
            targetGain   = processor.livePartialState[i].effectiveGain.load(std::memory_order_relaxed);
            targetSemi   = processor.livePartialState[i].effectiveSemitone.load(std::memory_order_relaxed);
            targetPan    = processor.livePartialState[i].effectivePan.load(std::memory_order_relaxed);
            float voicePhase = processor.livePartialState[i].animPhase.load(std::memory_order_relaxed);
            previewAnimPhases[i] = voicePhase;
            if (i == selectedPartial && !isScrubbingTimeline && draggedKeyframeIndex < 0 && draggedPartial < 0)
                scrubTimeNorm = voicePhase;
        }
        else
        {
            // Voice not active (or node being dragged)
            PartialAnimationData track = processor.getPartialAnimation(i);

            // 1. Keyframe Animation Evaluation
            if (animMode > 0 && track.numKeyframes > 0)
            {
                float kfPhase = 0.0f;
                if (!isLocked && animMode >= 2) // Time-based modes: 2=One-Shot, 3=Loop, 4=Ping-Pong
                {
                    previewAnimElapsed[i] += dt;
                    float progress = previewAnimElapsed[i] / animDur;
                    if (animMode == 2)
                        kfPhase = juce::jlimit(0.0f, 1.0f, progress);
                    else if (animMode == 3)
                        kfPhase = std::fmod(progress, 1.0f);
                    else if (animMode == 4)
                    {
                        float cycle = std::fmod(progress, 2.0f);
                        kfPhase = (cycle <= 1.0f) ? cycle : (2.0f - cycle);
                    }
                }
                else
                {
                    kfPhase = isTimelineOpen ? scrubTimeNorm : 0.0f;
                }

                previewAnimPhases[i] = kfPhase;
                if (!voiceActive && !isLocked && i == selectedPartial && isTimelineOpen && !isScrubbingTimeline && draggedKeyframeIndex < 0 && draggedPartial < 0)
                    scrubTimeNorm = kfPhase;

                const PartialKeyframe& startKf = track.keyframes[0];
                PartialKeyframe evalKf = track.evaluate(kfPhase, animCurve, startKf);
                float effDepth = (isTimelineOpen || draggedPartial == i) ? 1.0f : animDepth;
                targetRatio  = juce::jlimit(0.01f, 32.0f,  startKf.ratio    + effDepth * (evalKf.ratio    - startKf.ratio));
                targetGain   = juce::jlimit(0.0f,  1.0f,   startKf.gain     + effDepth * (evalKf.gain     - startKf.gain));
                targetSemi   = juce::jlimit(-24.0f, 24.0f, startKf.semitone + effDepth * (evalKf.semitone - startKf.semitone));
                targetPan    = juce::jlimit(-1.0f,  1.0f,   startKf.pan      + effDepth * (evalKf.pan      - startKf.pan));
            }

            // 2. LFO Modulation Preview (when not locked)
            if (!isLocked && draggedPartial != i && modTarget > 0 && modDepth > 0.001f)
            {
                editorLfoPhases[i] += modRate * dt;
                if (editorLfoPhases[i] >= 1.0f) editorLfoPhases[i] -= 1.0f;

                float ph = editorLfoPhases[i] + (modPhase / 360.0f);
                ph = std::fmod(ph, 1.0f);
                if (ph < 0.0f) ph += 1.0f;

                float lfoVal = 0.0f;
                switch (modWave)
                {
                    case 0: lfoVal = std::sin(ph * juce::MathConstants<float>::twoPi); break;
                    case 1: lfoVal = 2.0f * std::abs(2.0f * ph - 1.0f) - 1.0f; break; // Tri
                    case 2: lfoVal = 2.0f * ph - 1.0f; break; // Saw
                    case 3: lfoVal = ph < 0.5f ? 1.0f : -1.0f; break; // Square
                    default:
                    {
                        int slot = juce::jlimit(0, 3, modWave - 4);
                        int idx = juce::jlimit(0, 511, (int)(ph * 511.0f));
                        lfoVal = processor.customWavetables[slot][idx].load(std::memory_order_relaxed);
                        break;
                    }
                }

                if (modTarget == 1)      targetSemi = juce::jlimit(-24.0f, 24.0f, targetSemi + lfoVal * modDepth * 12.0f);
                else if (modTarget == 2) targetGain = juce::jlimit(0.0f, 1.0f, targetGain * (1.0f - modDepth * 0.5f * (1.0f - lfoVal)));
                else if (modTarget == 3) targetPan  = juce::jlimit(-1.0f, 1.0f, targetPan + lfoVal * modDepth);
            }

            targetPan = juce::jlimit(-1.0f, 1.0f, masterPan + targetPan);
            targetRatio = juce::jlimit(0.01f, 32.0f, targetRatio * std::pow(2.0f, targetSemi / 12.0f));
        }

        if (!smoothedInit)
        {
            smoothedModRatio[i]  = targetRatio;
            smoothedModGain[i]   = targetGain;
            smoothedModSemi[i]   = targetSemi;
            smoothedModDetune[i] = 0.0f;
            smoothedModPan[i]    = targetPan;
        }
        else
        {
            const float smoothAlpha = 0.62f;
            smoothedModRatio[i]  += smoothAlpha * (targetRatio  - smoothedModRatio[i]);
            smoothedModGain[i]   += smoothAlpha * (targetGain   - smoothedModGain[i]);
            smoothedModSemi[i]   += smoothAlpha * (targetSemi   - smoothedModSemi[i]);
            smoothedModDetune[i] = 0.0f;
            smoothedModPan[i]    += smoothAlpha * (targetPan    - smoothedModPan[i]);
        }
    }
    smoothedInit = true;

    drawNextFrameOfSpectrum();
    updateWaterfallDots();
    updateWaterfallRow();
    repaint();
    repaintTimeline();
}


