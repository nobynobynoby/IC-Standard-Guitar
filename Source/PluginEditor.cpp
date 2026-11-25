#include "PluginEditor.h"

#include <cmath>

namespace
{

const juce::Colour kBgTop         { 0xff16181e };
const juce::Colour kBgBottom      { 0xff0d0e12 };
const juce::Colour kPanelBg       { 0xff1e212a };
const juce::Colour kPanelBorder   { 0xff2d3240 };
const juce::Colour kAccentGold    { 0xfff59e0b };
const juce::Colour kAccentCyan    { 0xff38bdf8 };
const juce::Colour kAccentGreen   { 0xff34d399 };
const juce::Colour kAccentRed     { 0xfff87171 };
const juce::Colour kTextPrimary   { 0xfff3f4f6 };
const juce::Colour kTextMuted     { 0xff9ca3af };

juce::Font makeJpFont(float size, bool bold = false)
{
#if JUCE_WINDOWS
    static const char* const kJpFontName = "Yu Gothic UI";
#else
    static const char* const kJpFontName = "Hiragino Sans";
#endif
    return juce::Font(juce::FontOptions(kJpFontName, size, bold ? juce::Font::bold : juce::Font::plain));
}

juce::String formatMidiNoteName(int midiNote)
{
    if (midiNote < 0 || midiNote > 127)
        return "--";
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int octave = (midiNote / 12) - 1;
    return juce::String(names[midiNote % 12]) + juce::String(octave);
}

} // namespace

ICStandardGuitarEditor::GuitarLookAndFeel::GuitarLookAndFeel()
{
    setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff252934));
    setColour(juce::ComboBox::textColourId, kTextPrimary);
    setColour(juce::ComboBox::outlineColourId, kPanelBorder);
    setColour(juce::ComboBox::arrowColourId, kAccentGold);

    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff20242e));
    setColour(juce::PopupMenu::textColourId, kTextPrimary);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, kAccentGold.withAlpha(0.25f));
    setColour(juce::PopupMenu::highlightedTextColourId, kAccentGold);

    setColour(juce::TextButton::buttonColourId, juce::Colour(0xff262b37));
    setColour(juce::TextButton::buttonOnColourId, kAccentGold.withAlpha(0.25f));
    setColour(juce::TextButton::textColourOffId, kTextPrimary);
    setColour(juce::TextButton::textColourOnId, kAccentGold);
}

void ICStandardGuitarEditor::GuitarLookAndFeel::drawRotarySlider(
    juce::Graphics& g,
    int x, int y, int width, int height,
    float sliderPos,
    float rotaryStartAngle,
    float rotaryEndAngle,
    juce::Slider& /*slider*/)
{
    const float radius = static_cast<float>(juce::jmin(width, height)) * 0.40f;
    const float cx = static_cast<float>(x) + static_cast<float>(width) * 0.5f;
    const float cy = static_cast<float>(y) + static_cast<float>(height) * 0.5f;
    const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    // Outer track arc
    juce::Path trackArc;
    trackArc.addCentredArc(cx, cy, radius, radius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour(juce::Colour(0xff2e3442));
    g.strokePath(trackArc, juce::PathStrokeType(3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Active value arc
    if (sliderPos > 0.001f)
    {
        juce::Path valueArc;
        valueArc.addCentredArc(cx, cy, radius, radius, 0.0f, rotaryStartAngle, angle, true);
        g.setColour(kAccentGold);
        g.strokePath(valueArc, juce::PathStrokeType(3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Metallic amp knob cap
    const float capRadius = radius * 0.72f;
    juce::ColourGradient capGrad(juce::Colour(0xff343a48), cx - capRadius * 0.4f, cy - capRadius * 0.4f,
                                 juce::Colour(0xff191c23), cx + capRadius * 0.6f, cy + capRadius * 0.6f, false);
    g.setGradientFill(capGrad);
    g.fillEllipse(cx - capRadius, cy - capRadius, capRadius * 2.0f, capRadius * 2.0f);

    g.setColour(juce::Colour(0xff474f62));
    g.drawEllipse(cx - capRadius, cy - capRadius, capRadius * 2.0f, capRadius * 2.0f, 1.0f);

    // Pointer indicator
    juce::Path pointer;
    const float pointerLen = capRadius * 0.72f;
    const float pointerThickness = 2.6f;
    pointer.addRoundedRectangle(-pointerThickness * 0.5f, -capRadius + 3.0f, pointerThickness, pointerLen, 1.2f);
    pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(cx, cy));
    g.setColour(kAccentGold);
    g.fillPath(pointer);
}

juce::Font ICStandardGuitarEditor::GuitarLookAndFeel::getComboBoxFont(juce::ComboBox&)
{
    return makeJpFont(13.0f, false);
}

juce::Font ICStandardGuitarEditor::GuitarLookAndFeel::getPopupMenuFont()
{
    return makeJpFont(13.0f, false);
}

juce::Font ICStandardGuitarEditor::GuitarLookAndFeel::getTextButtonFont(juce::TextButton&, int)
{
    return makeJpFont(12.5f, true);
}

ICStandardGuitarEditor::ICStandardGuitarEditor(ICStandardGuitarProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setLookAndFeel(&customLookAndFeel);
    setSize(720, 460);

    // Articulation selector
    articulationLabel.setText(juce::String::fromUTF8("奏法モード (Articulation)"), juce::dontSendNotification);
    articulationLabel.setFont(makeJpFont(11.5f, true));
    articulationLabel.setColour(juce::Label::textColourId, kTextMuted);
    articulationLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(articulationLabel);

    articulationBox.addItem(juce::String::fromUTF8("Auto Strum (自動ダウン/アップ)"), 1);
    articulationBox.addItem(juce::String::fromUTF8("Sustain Down (ダウン固定)"), 2);
    articulationBox.addItem(juce::String::fromUTF8("Sustain Up (アップ固定)"), 3);
    articulationBox.addItem(juce::String::fromUTF8("Palm Mute (自動D/Uミュート)"), 4);
    articulationBox.addItem(juce::String::fromUTF8("Finger Mute (カッティング)"), 5);
    articulationBox.addItem(juce::String::fromUTF8("Velocity Switch (弱:Mute / 強:Sus)"), 6);
    addAndMakeVisible(articulationBox);

    articulationAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getAPVTS(), "articulationMode", articulationBox);

    // Rotary knobs
    setupKnob(attackTrimKnob, "attackTrim",
              juce::String::fromUTF8("レイテンシー除去"),
              [](double v) { return juce::String(juce::roundToInt(v * 100.0)) + "%"; });

    setupKnob(releaseTimeKnob, "releaseTime",
              juce::String::fromUTF8("キーオフ余韻"),
              [](double v) { return juce::String(v, 2) + " s"; });

    setupKnob(damperSoftnessKnob, "damperSoftness",
              juce::String::fromUTF8("弦ミュート丸み"),
              [](double v) { return juce::String(juce::roundToInt(v * 100.0)) + "%"; });

    setupKnob(releaseNoiseKnob, "releaseNoise",
              juce::String::fromUTF8("フレットノイズ"),
              [](double v) { return juce::String(juce::roundToInt(v * 100.0)) + "%"; });

    setupKnob(toneEqKnob, "toneEq",
              juce::String::fromUTF8("ギタートーン"),
              [](double v) { return juce::String(juce::roundToInt(v * 100.0)) + "%"; });

    setupKnob(ambienceKnob, "ambience",
              juce::String::fromUTF8("胴鳴り・空間"),
              [](double v) { return juce::String(juce::roundToInt(v * 100.0)) + "%"; });

    setupKnob(outputGainKnob, "outputGain",
              juce::String::fromUTF8("出力音量"),
              [](double v) {
                  const juce::String sign = (v > 0.05) ? "+" : "";
                  return sign + juce::String(v, 1) + " dB";
              });

    // Buttons
    chooseFolderButton.onClick = [this] { chooseSamplesFolder(); };
    addAndMakeVisible(chooseFolderButton);

    auditionDownButton.onClick = [this] { audioProcessor.triggerTestStrum(false, false); };
    auditionUpButton.onClick = [this] { audioProcessor.triggerTestStrum(true, false); };
    auditionMuteButton.onClick = [this] { audioProcessor.triggerTestStrum(false, true); };
    auditionReleaseButton.onClick = [this] { audioProcessor.triggerTestRelease(); };

    auditionReleaseButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff382428));
    auditionReleaseButton.setColour(juce::TextButton::textColourOffId, kAccentRed);

    addAndMakeVisible(auditionDownButton);
    addAndMakeVisible(auditionUpButton);
    addAndMakeVisible(auditionMuteButton);
    addAndMakeVisible(auditionReleaseButton);

    startTimerHz(20);
}

ICStandardGuitarEditor::~ICStandardGuitarEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void ICStandardGuitarEditor::setupKnob(KnobControl& kc,
                                       const juce::String& paramId,
                                       const juce::String& title,
                                       std::function<juce::String(double)> formatter)
{
    kc.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    kc.slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    kc.slider.setPopupDisplayEnabled(false, false, this);
    addAndMakeVisible(kc.slider);

    kc.titleLabel.setText(title, juce::dontSendNotification);
    kc.titleLabel.setFont(makeJpFont(11.5f, true));
    kc.titleLabel.setColour(juce::Label::textColourId, kTextPrimary);
    kc.titleLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(kc.titleLabel);

    kc.valueLabel.setFont(makeJpFont(11.0f, false));
    kc.valueLabel.setColour(juce::Label::textColourId, kAccentGold);
    kc.valueLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(kc.valueLabel);

    kc.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), paramId, kc.slider);

    kc.slider.onValueChange = [this, &kc, formatter]() mutable {
        updateKnobValueLabel(kc, formatter);
    };
    updateKnobValueLabel(kc, formatter);
}

void ICStandardGuitarEditor::updateKnobValueLabel(KnobControl& kc,
                                                  const std::function<juce::String(double)>& formatter)
{
    kc.valueLabel.setText(formatter(kc.slider.getValue()), juce::dontSendNotification);
}

void ICStandardGuitarEditor::chooseSamplesFolder()
{
    const juce::File initialDir(audioProcessor.getSamplesDirectoryPath());
    fileChooser = std::make_unique<juce::FileChooser>(
        juce::String::fromUTF8("UI_Standard_Guitar の Samples フォルダを選択"),
        initialDir.exists() ? initialDir : juce::File::getSpecialLocation(juce::File::userMusicDirectory));

    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                             [this](const juce::FileChooser& chooser)
                             {
                                 const auto result = chooser.getResult();
                                 if (result.isDirectory())
                                     audioProcessor.setSamplesDirectory(result);
                             });
}

void ICStandardGuitarEditor::timerCallback()
{
    repaint(0, 0, getWidth(), 106);
}

void ICStandardGuitarEditor::paint(juce::Graphics& g)
{
    // Background gradient
    juce::ColourGradient bgGrad(kBgTop, 0.0f, 0.0f, kBgBottom, 0.0f, static_cast<float>(getHeight()), false);
    g.setGradientFill(bgGrad);
    g.fillAll();

    // Header card
    const auto headerArea = juce::Rectangle<float>(16.0f, 14.0f, static_cast<float>(getWidth() - 32), 84.0f);
    g.setColour(kPanelBg);
    g.fillRoundedRectangle(headerArea, 10.0f);
    g.setColour(kPanelBorder);
    g.drawRoundedRectangle(headerArea, 10.0f, 1.0f);

    // Title & subtitle
    g.setColour(kTextPrimary);
    g.setFont(makeJpFont(19.0f, true));
    g.drawText("IC Standard Guitar", 30, 22, 260, 26, juce::Justification::centredLeft);

    g.setColour(kAccentGold);
    g.setFont(makeJpFont(11.5f, true));
    g.drawText(juce::String::fromUTF8("InstaChord 専用低遅延エンジン"), 30, 48, 250, 18, juce::Justification::centredLeft);

    // Sample status badge
    const auto& bank = audioProcessor.getSampleBank();
    const bool loading = bank.isLoading();
    const int loadedCount = bank.getLoadedCount();
    const int totalCount = bank.getTotalTargetCount();
    const juce::String errMsg = bank.getLastError();

    juce::String statusText;
    juce::Colour statusColour = kAccentGreen;
    if (loading)
    {
        const int pct = juce::roundToInt(bank.getProgress() * 100.0f);
        statusText = juce::String::fromUTF8("サンプル読込中... ") + juce::String(pct) + "% ("
                     + juce::String(loadedCount) + "/" + juce::String(totalCount) + ")";
        statusColour = kAccentGold;
    }
    else if (loadedCount > 0)
    {
        statusText = juce::String::fromUTF8("✓ 読込完了 (") + juce::String(loadedCount) + " samples)";
        statusColour = kAccentGreen;
    }
    else
    {
        statusText = errMsg.isNotEmpty() ? errMsg : juce::String::fromUTF8("⚠ Samplesフォルダ未設定");
        statusColour = kAccentRed;
    }

    g.setColour(statusColour.withAlpha(0.14f));
    g.fillRoundedRectangle(292.0f, 22.0f, 270.0f, 28.0f, 6.0f);
    g.setColour(statusColour);
    g.setFont(makeJpFont(11.5f, true));
    g.drawText(statusText, 300, 22, 254, 28, juce::Justification::centredLeft, true);

    // Live MIDI & Strum badge
    const auto& eng = audioProcessor.getEngine();
    const bool lastUp = eng.wasLastStrumUp();
    const int activeVoices = eng.getActiveVoiceCount();
    const int lastNote = eng.getLastPlayedNote();

    const juce::String strumText = (lastNote < 0)
        ? juce::String::fromUTF8("MIDI 待機中")
        : (lastUp ? juce::String::fromUTF8("↑ UP STRUM") : juce::String::fromUTF8("↓ DOWN STRUM"));
    const juce::Colour strumColour = (lastNote < 0) ? kTextMuted : (lastUp ? kAccentCyan : kAccentGold);

    g.setColour(juce::Colour(0xff242834));
    g.fillRoundedRectangle(292.0f, 56.0f, 400.0f, 30.0f, 6.0f);

    g.setColour(strumColour);
    g.setFont(makeJpFont(12.0f, true));
    g.drawText(strumText, 304, 56, 130, 30, juce::Justification::centredLeft);

    g.setColour(kTextPrimary);
    g.setFont(makeJpFont(11.5f, false));
    g.drawText(juce::String::fromUTF8("発音中: ") + juce::String(activeVoices) + " voices   |   Note: "
               + formatMidiNoteName(lastNote),
               442, 56, 240, 30, juce::Justification::centredRight);

    // Main control cards
    const auto topRowCard = juce::Rectangle<float>(16.0f, 110.0f, static_cast<float>(getWidth() - 32), 138.0f);
    g.setColour(kPanelBg);
    g.fillRoundedRectangle(topRowCard, 10.0f);
    g.setColour(kPanelBorder);
    g.drawRoundedRectangle(topRowCard, 10.0f, 1.0f);

    g.setColour(kAccentCyan);
    g.setFont(makeJpFont(11.0f, true));
    g.drawText(juce::String::fromUTF8("INSTACHORD 演奏フィーリング調整"), 30, 116, 300, 18, juce::Justification::centredLeft);

    const auto bottomRowCard = juce::Rectangle<float>(16.0f, 258.0f, static_cast<float>(getWidth() - 32), 138.0f);
    g.setColour(kPanelBg);
    g.fillRoundedRectangle(bottomRowCard, 10.0f);
    g.setColour(kPanelBorder);
    g.drawRoundedRectangle(bottomRowCard, 10.0f, 1.0f);

    g.setColour(kAccentGold);
    g.setFont(makeJpFont(11.0f, true));
    g.drawText(juce::String::fromUTF8("奏法モード & サウンド出力"), 30, 264, 300, 18, juce::Justification::centredLeft);
}

void ICStandardGuitarEditor::resized()
{
    chooseFolderButton.setBounds(572, 22, 120, 28);

    auto layoutKnob = [](KnobControl& kc, juce::Rectangle<int> cell)
    {
        kc.titleLabel.setBounds(cell.removeFromTop(18));
        kc.valueLabel.setBounds(cell.removeFromBottom(18));
        kc.slider.setBounds(cell.reduced(6, 0));
    };

    // Row 1: 4 InstaChord response/release knobs
    const int row1Y = 136;
    const int row1H = 104;
    const int colW = (getWidth() - 56) / 4;
    layoutKnob(attackTrimKnob,     { 28 + colW * 0, row1Y, colW, row1H });
    layoutKnob(releaseTimeKnob,    { 28 + colW * 1, row1Y, colW, row1H });
    layoutKnob(damperSoftnessKnob, { 28 + colW * 2, row1Y, colW, row1H });
    layoutKnob(releaseNoiseKnob,   { 28 + colW * 3, row1Y, colW, row1H });

    // Row 2: Articulation selector (left) + 3 Tone/Ambience/Gain knobs (right)
    const int row2Y = 284;
    const int row2H = 104;
    articulationLabel.setBounds(30, 294, 210, 20);
    articulationBox.setBounds(30, 318, 220, 32);

    const int rightStart = 266;
    const int rightColW = (getWidth() - rightStart - 24) / 3;
    layoutKnob(toneEqKnob,     { rightStart + rightColW * 0, row2Y, rightColW, row2H });
    layoutKnob(ambienceKnob,   { rightStart + rightColW * 1, row2Y, rightColW, row2H });
    layoutKnob(outputGainKnob, { rightStart + rightColW * 2, row2Y, rightColW, row2H });

    // Bottom audition bar
    const int btnY = 410;
    const int btnH = 34;
    const int gap = 10;
    const int btnW = (getWidth() - 32 - gap * 3) / 4;
    auditionDownButton.setBounds(16 + (btnW + gap) * 0, btnY, btnW, btnH);
    auditionUpButton.setBounds(16 + (btnW + gap) * 1, btnY, btnW, btnH);
    auditionMuteButton.setBounds(16 + (btnW + gap) * 2, btnY, btnW, btnH);
    auditionReleaseButton.setBounds(16 + (btnW + gap) * 3, btnY, btnW, btnH);
}
