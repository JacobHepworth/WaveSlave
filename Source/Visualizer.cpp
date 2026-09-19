#include "Visualizer.h"

//==============================================================================
// Oscilloscope

Oscilloscope::Oscilloscope(WaveSlaveAudioProcessor& p) : processor(p)
{
    displayBuffer.setSize(1, 512);
    displayBuffer.clear();
    partialDisplayBuffer.setSize(16, 512);
    partialDisplayBuffer.clear();

    overlayBtn.setClickingTogglesState(true);
    overlayBtn.setToggleState(true, juce::dontSendNotification);
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
        repaint();
    };
    addAndMakeVisible(drawBtn);

    bezierToggle.setColour(juce::ToggleButton::textColourId, juce::Colours::white);
    bezierToggle.setToggleState(true, juce::dontSendNotification);
    bezierToggle.onClick = [this]() { repaint(); };
    addChildComponent(bezierToggle); // hidden by default

    startTimerHz(30);
}

Oscilloscope::~Oscilloscope()
{
}

void Oscilloscope::resized()
{
    // Move buttons to the bottom of the graph. Distribute evenly.
    int btnWidth = getWidth() / 3;
    drawBtn.setBounds(0, getHeight() - 24, btnWidth, 18);
    polarBtn.setBounds(btnWidth, getHeight() - 24, btnWidth, 18);
    overlayBtn.setBounds(btnWidth * 2, getHeight() - 24, getWidth() - btnWidth * 2, 18);
    bezierToggle.setBounds(getWidth() - 84, 4, 80, 20); // Top-right corner
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

    // Label at top
    g.setColour(juce::Colours::white.withAlpha(0.6f));
    g.setFont(11.0f);
    g.drawText(isPolar ? "POLAR OSCILLOSCOPE" : "OSCILLOSCOPE", 12, 6, 200, 14, juce::Justification::centredLeft, false);

    // Inner graph area reserved between top title (22px) and bottom buttons (28px)
    auto graphArea = bounds.reduced(12.0f);
    graphArea.removeFromTop(12.0f);
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
        g.setColour(juce::Colour(0x554FA0F0));
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
    g.setColour(juce::Colour(0x334FA0F0));
    g.strokePath(p, juce::PathStrokeType(6.0f));

    // Main line (Composite)
    g.setColour(juce::Colour(0xFFFFFFFF));
    g.strokePath(p, juce::PathStrokeType(1.6f));

    if (isDrawMode)
    {
        g.setColour(juce::Colours::red.withAlpha(0.6f));
        g.drawRect(graphArea, 2.0f);
        g.drawText("DRAWING CUSTOM WAVE", graphArea.reduced(4), juce::Justification::topRight, false);

        if (bezierToggle.getToggleState())
        {
            float maxVisualH = height * 0.38f;
            float maxRadius = std::min(width, height) * 0.45f;
            g.setColour(juce::Colours::orange);
            for (const auto& pt : splinePoints)
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
                g.fillEllipse(px - 3.0f, py - 3.0f, 6.0f, 6.0f);
            }
        }
    }
}

void Oscilloscope::updateWavetableFromSpline()
{
    if (splinePoints.empty()) return;

    std::sort(splinePoints.begin(), splinePoints.end(), [](const juce::Point<float>& a, const juce::Point<float>& b) {
        return a.x < b.x;
    });

    for (int i = 0; i < 512; ++i)
    {
        float x = (float)i / 511.0f;
        
        if (x <= splinePoints.front().x) {
            processor.customWavetable[i].store(splinePoints.front().y, std::memory_order_relaxed);
            continue;
        }
        if (x >= splinePoints.back().x) {
            processor.customWavetable[i].store(splinePoints.back().y, std::memory_order_relaxed);
            continue;
        }

        int seg = 0;
        for (; seg < (int)splinePoints.size() - 1; ++seg) {
            if (x >= splinePoints[seg].x && x <= splinePoints[seg + 1].x) break;
        }

        float x0 = splinePoints[seg].x;
        float x1 = splinePoints[seg + 1].x;
        float t = (x1 > x0) ? (x - x0) / (x1 - x0) : 0.0f;

        float y0 = (seg - 1 >= 0) ? splinePoints[seg - 1].y : splinePoints[seg].y;
        float y1 = splinePoints[seg].y;
        float y2 = splinePoints[seg + 1].y;
        float y3 = (seg + 2 < (int)splinePoints.size()) ? splinePoints[seg + 2].y : splinePoints[seg + 1].y;

        float t2 = t * t;
        float t3 = t2 * t;
        float y = 0.5f * (
            (2.0f * y1) +
            (-y0 + y2) * t +
            (2.0f * y0 - 5.0f * y1 + 4.0f * y2 - y3) * t2 +
            (-y0 + 3.0f * y1 - 3.0f * y2 + y3) * t3
        );

        y = juce::jlimit(-1.0f, 1.0f, y);
        processor.customWavetable[i].store(y, std::memory_order_relaxed);
    }
}

void Oscilloscope::clearWavetable()
{
    splinePoints.clear();
    for (int i = 0; i < 512; ++i)
    {
        processor.customWavetable[i].store(0.0f, std::memory_order_relaxed);
    }
    repaint();
}

void Oscilloscope::drawOnWavetable(const juce::MouseEvent& e, bool isDrag, bool isRelease)
{
    if (!isDrawMode) return;

    auto bounds = getLocalBounds().toFloat();
    auto graphArea = bounds.reduced(12.0f);
    graphArea.removeFromTop(12.0f);
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
    processor.customWavetable[idx].store(normY, std::memory_order_relaxed);
    
    static int lastIdx = -1;
    if (isDrag && lastIdx != -1)
    {
        int start = std::min(lastIdx, idx);
        int end = std::max(lastIdx, idx);
        if (end - start > 1 && end - start < 200)
        {
            float vStart = processor.customWavetable[start].load(std::memory_order_relaxed);
            float vEnd = processor.customWavetable[end].load(std::memory_order_relaxed);
            for (int i = start + 1; i < end; ++i)
            {
                float interp = (float)(i - start) / (float)(end - start);
                processor.customWavetable[i].store(vStart + interp * (vEnd - vStart), std::memory_order_relaxed);
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
    if (isDrawMode)
    {
        for (int i = 0; i < 512; ++i)
        {
            float val = processor.customWavetable[i].load(std::memory_order_relaxed);
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

    showNodesBtn.setClickingTogglesState(true);
    showNodesBtn.setToggleState(true, juce::dontSendNotification);
    showNodesBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF444444));
    showNodesBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFFFFFFFF).withAlpha(0.25f));
    showNodesBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white.withAlpha(0.5f));
    showNodesBtn.setColour(juce::TextButton::textColourOnId, juce::Colour(0xFFFFFFFF));
    showNodesBtn.onClick = [this]() {
        setReadOnly(!showNodesBtn.getToggleState());
    };
    addAndMakeVisible(showNodesBtn);

    staticModeBtn.setClickingTogglesState(true);
    staticModeBtn.setToggleState(false, juce::dontSendNotification);
    staticModeBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF444444));
    staticModeBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFFFFFFFF).withAlpha(0.25f));
    staticModeBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white.withAlpha(0.5f));
    staticModeBtn.setColour(juce::TextButton::textColourOnId, juce::Colour(0xFFFFFFFF));
    staticModeBtn.onClick = [this]() {
        isStaticMode = staticModeBtn.getToggleState();
        repaint();
    };
    addAndMakeVisible(staticModeBtn);

    lockNodesBtn.setClickingTogglesState(true);
    lockNodesBtn.setToggleState(false, juce::dontSendNotification);
    lockNodesBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF444444));
    lockNodesBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFFFFFFFF).withAlpha(0.25f));
    lockNodesBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white.withAlpha(0.5f));
    lockNodesBtn.setColour(juce::TextButton::textColourOnId, juce::Colour(0xFFFFFFFF));
    lockNodesBtn.onClick = [this]() {
        isNodesLocked = lockNodesBtn.getToggleState();
        repaint();
    };
    addAndMakeVisible(lockNodesBtn);

    startTimerHz(30);
}

Spectrograph::~Spectrograph()
{
}

void Spectrograph::resized()
{
    showNodesBtn.setBounds(getWidth() - 96, getHeight() - 24, 88, 18);
    staticModeBtn.setBounds(getWidth() - 192, getHeight() - 24, 92, 18);
    lockNodesBtn.setBounds(getWidth() - 280, getHeight() - 24, 84, 18);
}

void Spectrograph::drawNextFrameOfSpectrum()
{
    window.multiplyWithWindowingTable(fftData, fftSize);
    forwardFFT.performFrequencyOnlyForwardTransform(fftData);

    auto mindB = -100.0f;
    auto maxdB =    0.0f;

    float sampleRate = processor.getSampleRate();
    if (sampleRate <= 0.0f) sampleRate = 44100.0f;
    float baseFreq = processor.lastPlayedFrequency.load(std::memory_order_relaxed);
    if (isStaticMode && isNodesLocked) {
        if (auto* bn = processor.apvts.getParameter("BASE_NOTE")) {
            int note = (int)bn->convertFrom0to1(bn->getValue());
            baseFreq = (float)juce::MidiMessage::getMidiNoteInHertz(note + 23);
        }
    }
    if (baseFreq <= 0.0f) baseFreq = 440.0f;

    if (auto* rParam = processor.apvts.getParameter("RATIO1"))
    {
        for (int i = 0; i < scopeSize; ++i)
        {
            float freq = 0.0f;
            float norm = (float)i / (float)(scopeSize - 1);
            if (isStaticMode) {
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
    auto left   = inner.getX();
    auto top    = inner.getY() + 24.0f;
    auto width  = inner.getWidth();
    // Reserve 28px at the bottom for harmonic labels and frequencies
    auto height = inner.getHeight() - 52.0f;

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

    float baseFreq = processor.lastPlayedFrequency.load(std::memory_order_relaxed);
    if (isStaticMode && isNodesLocked) {
        if (auto* bn = processor.apvts.getParameter("BASE_NOTE")) {
            int note = (int)bn->convertFrom0to1(bn->getValue());
            baseFreq = (float)juce::MidiMessage::getMidiNoteInHertz(note + 23);
        }
    }
    if (baseFreq <= 0.0f) baseFreq = 440.0f;

    // Vertical guide lines (Static vs Ratio)
    if (isStaticMode)
    {
        // Draw fixed frequency guides (e.g. 100Hz, 1kHz, 10kHz)
        std::vector<float> freqs = { 100.0f, 1000.0f, 10000.0f };
        std::vector<juce::String> labels = { "100Hz", "1kHz", "10kHz" };
        for (size_t i = 0; i < freqs.size(); ++i)
        {
            float f = freqs[i];
            float normX = std::log10(f / 20.0f) / std::log10(20000.0f / 20.0f);
            float gx = left + normX * width;
            if (normX >= 0.0f && normX <= 1.0f)
            {
                g.setColour(juce::Colour(0x18FFFFFF));
                g.drawVerticalLine((int)gx, top, top + height);
                g.setColour(juce::Colour(0x60FFFFFF));
                g.setFont(9.0f);
                g.drawText(labels[i], (int)gx - 20, (int)(top + height + 1), 40, 11, juce::Justification::centred);
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

    // Filled gradient under the curve
    juce::ColourGradient grad(juce::Colour(0x444FA0F0), left, top,
                               juce::Colour(0x004FA0F0), left, top + height, false);
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

    g.setColour(juce::Colour(0x224FA0F0));
    g.strokePath(linePath, juce::PathStrokeType(4.0f));
    g.setColour(juce::Colour(0xAA4FA0F0));
    g.strokePath(linePath, juce::PathStrokeType(1.2f));

    if (!isReadOnly)
    {
        // Interactive Harmonic Nodes (Pucks & Stems)
        int numActive = (int)processor.apvts.getRawParameterValue("NUM_PARTIALS")->load();
        numActive = juce::jlimit(0, 16, numActive);

        int activeTarget = (draggedPartial >= 0) ? draggedPartial : hoveredPartial;
    if (activeTarget < 0 && selectedPartial >= 0 && selectedPartial < numActive)
        activeTarget = selectedPartial;

    // Overlapping Harmonic Bell Curves
    for (int i = 0; i < numActive; ++i)
    {
        auto idSuffix = juce::String(i + 1);
        auto* rp = processor.apvts.getParameter("RATIO" + idSuffix);
        auto* gp = processor.apvts.getParameter("GAIN" + idSuffix);
        if (!rp || !gp) continue;

        float normX = rp->getValue();
        float normY = gp->getValue();
        if (normY <= 0.005f) continue;

        float px = 0.0f;
        if (isStaticMode) {
            float ratioVal = rp->convertFrom0to1(normX);
            float harmFreq = ratioVal * baseFreq;
            float normStatic = std::log10(harmFreq / 20.0f) / std::log10(20000.0f / 20.0f);
            px = left + normStatic * width;
        } else {
            px = left + normX * width;
        }
        float peakH = normY * height;

        bool isSelected = (i == selectedPartial);
        bool isHovered  = (i == hoveredPartial);
        bool isDragged  = (i == draggedPartial);

        auto col = getPartialColour(i);

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

    // Interactive Harmonic Nodes (Pucks & Stems)
    for (int i = 0; i < numActive; ++i)
    {
        auto idSuffix = juce::String(i + 1);
        auto* rp = processor.apvts.getParameter("RATIO" + idSuffix);
        auto* gp = processor.apvts.getParameter("GAIN" + idSuffix);
        if (!rp || !gp) continue;

        float normX = rp->getValue();
        float normY = gp->getValue();
        float px = 0.0f;
        if (isStaticMode) {
            float ratioVal = rp->convertFrom0to1(normX);
            float harmFreq = ratioVal * baseFreq;
            float normStatic = std::log10(harmFreq / 20.0f) / std::log10(20000.0f / 20.0f);
            px = left + normStatic * width;
        } else {
            px = left + normX * width;
        }
        float py = (top + height) - normY * height;

        bool isSelected = (i == selectedPartial);
        bool isHovered  = (i == hoveredPartial);
        bool isDragged  = (i == draggedPartial);

        auto col = getPartialColour(i);

        // Vertical stem line
        if (isDragged || isHovered)
            g.setColour(col.brighter(0.4f));
        else if (isSelected)
            g.setColour(col);
        else
            g.setColour(col.withAlpha(0.6f));

        float strokeW = (isSelected || isHovered || isDragged) ? 2.0f : 1.2f;
        g.drawLine(px, top + height, px, py, strokeW);

        // Base pin dot
        g.setColour(col.withAlpha(0.8f));
        g.fillEllipse(px - 2.5f, top + height - 2.5f, 5.0f, 5.0f);

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

    // Floating HUD badge for active/hovered/dragged partial
    if (activeTarget >= 0 && activeTarget < numActive)
    {
        auto idSuffix = juce::String(activeTarget + 1);
        auto* rp = processor.apvts.getParameter("RATIO" + idSuffix);
        auto* gp = processor.apvts.getParameter("GAIN" + idSuffix);
        auto* sp = processor.apvts.getParameter("SEMITONE" + idSuffix);
        auto* dp = processor.apvts.getParameter("DETUNE" + idSuffix);

        if (rp && gp && sp && dp)
        {
            float normX = rp->getValue();
            float normY = gp->getValue();
            float px = 0.0f;
            if (isStaticMode) {
                float ratioVal = rp->convertFrom0to1(normX);
                float harmFreq = ratioVal * baseFreq;
                float normStatic = std::log10(harmFreq / 20.0f) / std::log10(20000.0f / 20.0f);
                px = left + normStatic * width;
            } else {
                px = left + normX * width;
            }
            float py = (top + height) - normY * height;

            float ratioVal = rp->convertFrom0to1(normX);
            float gainVal  = gp->convertFrom0to1(normY);
            float semiVal  = sp->convertFrom0to1(sp->getValue());
            float detVal   = dp->convertFrom0to1(dp->getValue());

            float harmFreq = ratioVal * baseFreq;
            juce::String fStr = (harmFreq >= 1000.0f)
                ? (juce::String(harmFreq / 1000.0f, 1) + " kHz")
                : (juce::String((int)std::round(harmFreq)) + " Hz");

            juce::String hudText = "H" + idSuffix + ": " + juce::String(ratioVal, 2) + "x (" + fStr + ")"
                                 + " | Gain " + juce::String((int)(gainVal * 100)) + "%";

            if (semiVal != 0.0f)
                hudText += " | " + juce::String(semiVal > 0 ? "+" : "") + juce::String((int)semiVal) + "st";
            if (detVal != 0.0f)
                hudText += " | " + juce::String(detVal > 0 ? "+" : "") + juce::String((int)detVal) + "ct";

            if (draggedPartial >= 0)
                hudText += " [DRAGGING]";

            int hudWidth = 230;
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
            g.setFont(10.5f);
            g.drawText(hudText, (int)hudX, (int)hudY, hudWidth, hudHeight, juce::Justification::centred);
        }
    }
    } // End of if (!isReadOnly)

    // Top Title & Quick Guide
    g.setColour(juce::Colour(0xFFFFFFFF)); // less neon
    g.setFont(juce::Font(11.0f, juce::Font::bold));
    juce::String titleStr = "SPECTRUM (Base: " + juce::String((int)std::round(baseFreq)) + " Hz)";
    g.drawText(titleStr, (int)left, 4, (int)width, 14, juce::Justification::centredLeft);

    g.setColour(juce::Colours::white.withAlpha(0.45f));
    g.setFont(9.5f);
    g.drawText("Drag: Ratio & Gain - Alt: Snap - Shift: Detune - Ctrl: Note", (int)left + 42, 18, (int)width - 42, 12, juce::Justification::centredLeft);
}

int Spectrograph::findPartialAt(float mouseX, float mouseY)
{
    auto inner = getLocalBounds().toFloat().reduced(12.0f);
    float left = inner.getX();
    float top = inner.getY() + 24.0f;
    float width = inner.getWidth();
    float height = inner.getHeight() - 52.0f;

    int numActive = (int)processor.apvts.getRawParameterValue("NUM_PARTIALS")->load();
    numActive = juce::jlimit(0, 16, numActive);

    float baseFreq = processor.lastPlayedFrequency.load(std::memory_order_relaxed);
    if (isStaticMode && isNodesLocked) {
        if (auto* bn = processor.apvts.getParameter("BASE_NOTE")) {
            int note = (int)bn->convertFrom0to1(bn->getValue());
            baseFreq = (float)juce::MidiMessage::getMidiNoteInHertz(note + 23);
        }
    }
    if (baseFreq <= 0.0f) baseFreq = 440.0f;

    // Search pucks in reverse order
    for (int i = numActive - 1; i >= 0; --i)
    {
        auto idSuffix = juce::String(i + 1);
        auto* rp = processor.apvts.getParameter("RATIO" + idSuffix);
        auto* gp = processor.apvts.getParameter("GAIN" + idSuffix);
        if (rp && gp)
        {
            float normX = rp->getValue();
            float px = 0.0f;
            if (isStaticMode) {
                float ratioVal = rp->convertFrom0to1(normX);
                float harmFreq = ratioVal * baseFreq;
                float normStatic = std::log10(harmFreq / 20.0f) / std::log10(20000.0f / 20.0f);
                px = left + normStatic * width;
            } else {
                px = left + normX * width;
            }
            float py = (top + height) - gp->getValue() * height;
            float dist = std::hypot(mouseX - px, mouseY - py);
            if (dist <= 16.0f)
                return i;
        }
    }

    // Search stems
    for (int i = numActive - 1; i >= 0; --i)
    {
        auto idSuffix = juce::String(i + 1);
        auto* rp = processor.apvts.getParameter("RATIO" + idSuffix);
        auto* gp = processor.apvts.getParameter("GAIN" + idSuffix);
        if (rp && gp)
        {
            float normX = rp->getValue();
            float px = 0.0f;
            if (isStaticMode) {
                float ratioVal = rp->convertFrom0to1(normX);
                float harmFreq = ratioVal * baseFreq;
                float normStatic = std::log10(harmFreq / 20.0f) / std::log10(20000.0f / 20.0f);
                px = left + normStatic * width;
            } else {
                px = left + normX * width;
            }
            float py = (top + height) - gp->getValue() * height;
            if (std::abs(mouseX - px) <= 8.0f && mouseY >= py - 4.0f && mouseY <= top + height + 4.0f)
                return i;
        }
    }

    return -1;
}

int Spectrograph::findClosestPartialX(float mouseX)
{
    auto inner = getLocalBounds().toFloat().reduced(12.0f);
    float left = inner.getX();
    float width = inner.getWidth();

    int numActive = (int)processor.apvts.getRawParameterValue("NUM_PARTIALS")->load();
    numActive = juce::jlimit(1, 16, numActive);

    float baseFreq = processor.lastPlayedFrequency.load(std::memory_order_relaxed);
    if (isStaticMode && isNodesLocked) {
        if (auto* bn = processor.apvts.getParameter("BASE_NOTE")) {
            int note = (int)bn->convertFrom0to1(bn->getValue());
            baseFreq = (float)juce::MidiMessage::getMidiNoteInHertz(note + 23);
        }
    }
    if (baseFreq <= 0.0f) baseFreq = 440.0f;

    int closest = -1;
    float minDist = 1e9f;
    for (int i = 0; i < numActive; ++i)
    {
        auto idSuffix = juce::String(i + 1);
        if (auto* rp = processor.apvts.getParameter("RATIO" + idSuffix))
        {
            float normX = rp->getValue();
            float px = 0.0f;
            if (isStaticMode) {
                float ratioVal = rp->convertFrom0to1(normX);
                float harmFreq = ratioVal * baseFreq;
                float normStatic = std::log10(harmFreq / 20.0f) / std::log10(20000.0f / 20.0f);
                px = left + normStatic * width;
            } else {
                px = left + normX * width;
            }
            float d = std::abs(mouseX - px);
            if (d < minDist)
            {
                minDist = d;
                closest = i;
            }
        }
    }
    return closest;
}

void Spectrograph::mouseMove(const juce::MouseEvent& e)
{
    if (isReadOnly) return;
    int hit = findPartialAt(e.position.x, e.position.y);
    if (hit != hoveredPartial)
    {
        hoveredPartial = hit;
        if (hoveredPartial >= 0)
            setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        else
            setMouseCursor(juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void Spectrograph::mouseExit(const juce::MouseEvent&)
{
    if (isReadOnly) return;
    if (hoveredPartial != -1)
    {
        hoveredPartial = -1;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void Spectrograph::mouseDown(const juce::MouseEvent& e)
{
    if (isReadOnly) return;

    int hit = findPartialAt(e.position.x, e.position.y);
    if (hit == -1)
    {
        hit = findClosestPartialX(e.position.x);
    }

    if (hit >= 0)
    {
        draggedPartial = hit;
        selectedPartial = hit;
        dragStartMouse = e.position;

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
        if (auto* dp = processor.apvts.getParameter("DETUNE" + idSuffix))
        {
            dp->beginChangeGesture();
            initialDetune = dp->convertFrom0to1(dp->getValue());
        }
        if (auto* sp = processor.apvts.getParameter("SEMITONE" + idSuffix))
        {
            sp->beginChangeGesture();
            initialSemitone = sp->convertFrom0to1(sp->getValue());
        }

        if (onPartialSelected)
            onPartialSelected(hit);
    }
}

void Spectrograph::mouseDrag(const juce::MouseEvent& e)
{
    if (isReadOnly) return;
    if (draggedPartial < 0) return;

    auto inner = getLocalBounds().toFloat().reduced(12.0f);
    float left = inner.getX();
    float top = inner.getY() + 24.0f;
    float width = inner.getWidth();
    float height = inner.getHeight() - 52.0f;

    auto idSuffix = juce::String(draggedPartial + 1);

    if (e.mods.isShiftDown())
    {
        // Shift: Fine Detune
        float deltaX = e.position.x - dragStartMouse.x;
        float newDetune = juce::jlimit(-100.0f, 100.0f, initialDetune + deltaX * 0.5f);
        if (auto* dp = processor.apvts.getParameter("DETUNE" + idSuffix))
            dp->setValueNotifyingHost(dp->convertTo0to1(newDetune));
    }
    else if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        // Ctrl/Cmd: Semitone offset
        float deltaY = dragStartMouse.y - e.position.y;
        float newSemi = juce::jlimit(-48.0f, 48.0f, initialSemitone + std::round(deltaY / 6.0f));
        if (auto* sp = processor.apvts.getParameter("SEMITONE" + idSuffix))
            sp->setValueNotifyingHost(sp->convertTo0to1(newSemi));
    }
    else
    {
        // Standard drag: X = Ratio, Y = Gain
        float normX = 0.0f;
        if (isStaticMode)
        {
            float normStatic = juce::jlimit(0.0f, 1.0f, (e.position.x - left) / width);
            float harmFreq = 20.0f * std::pow(20000.0f / 20.0f, normStatic);
            float baseFreq = processor.lastPlayedFrequency.load(std::memory_order_relaxed);
            if (baseFreq <= 0.0f) baseFreq = 440.0f;
            float ratioVal = harmFreq / baseFreq;
            if (auto* rp = processor.apvts.getParameter("RATIO" + idSuffix))
                normX = rp->convertTo0to1(ratioVal);
        }
        else
        {
            normX = juce::jlimit(0.0f, 1.0f, (e.position.x - left) / width);
        }
        float normY = juce::jlimit(0.0f, 1.0f, ((top + height) - e.position.y) / height);

        if (auto* rp = processor.apvts.getParameter("RATIO" + idSuffix))
        {
            if (e.mods.isAltDown())
            {
                // Alt: Snap ratio to nearest integer harmonic
                float ratioVal = rp->convertFrom0to1(normX);
                float snapped = std::round(ratioVal);
                snapped = juce::jlimit(0.1f, 10.0f, snapped);
                normX = rp->convertTo0to1(snapped);
            }
            rp->setValueNotifyingHost(normX);
        }

        if (auto* gp = processor.apvts.getParameter("GAIN" + idSuffix))
        {
            gp->setValueNotifyingHost(normY);
        }
    }

    repaint();
}

void Spectrograph::mouseUp(const juce::MouseEvent&)
{
    if (isReadOnly) return;
    if (draggedPartial >= 0)
    {
        auto idSuffix = juce::String(draggedPartial + 1);
        if (auto* rp = processor.apvts.getParameter("RATIO" + idSuffix))
            rp->endChangeGesture();
        if (auto* gp = processor.apvts.getParameter("GAIN" + idSuffix))
            gp->endChangeGesture();
        if (auto* dp = processor.apvts.getParameter("DETUNE" + idSuffix))
            dp->endChangeGesture();
        if (auto* sp = processor.apvts.getParameter("SEMITONE" + idSuffix))
            sp->endChangeGesture();

        draggedPartial = -1;
        repaint();
    }
}

void Spectrograph::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (isReadOnly) return;
    int hit = findPartialAt(e.position.x, e.position.y);
    if (hit >= 0)
    {
        auto idSuffix = juce::String(hit + 1);
        if (auto* rp = processor.apvts.getParameter("RATIO" + idSuffix))
            rp->setValueNotifyingHost(rp->convertTo0to1((float)(hit + 1)));

        if (auto* gp = processor.apvts.getParameter("GAIN" + idSuffix))
            gp->setValueNotifyingHost(gp->convertTo0to1(hit <= 3 ? 1.0f / (float)(hit + 1) : 0.0f));

        if (auto* dp = processor.apvts.getParameter("DETUNE" + idSuffix))
            dp->setValueNotifyingHost(dp->convertTo0to1(0.0f));

        if (auto* sp = processor.apvts.getParameter("SEMITONE" + idSuffix))
            sp->setValueNotifyingHost(sp->convertTo0to1(0.0f));

        repaint();
    }
}

void Spectrograph::timerCallback()
{
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

    drawNextFrameOfSpectrum();
    repaint();
}

