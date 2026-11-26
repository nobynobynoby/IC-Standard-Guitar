#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

class ICStandardGuitarEditor : public juce::AudioProcessorEditor,
                               private juce::Timer
{
public:
    explicit ICStandardGuitarEditor(ICStandardGuitarProcessor& processor);
    ~ICStandardGuitarEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    class GuitarLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        GuitarLookAndFeel();
        void drawRotarySlider(juce::Graphics& g,
                              int x, int y, int width, int height,
                              float sliderPosProportional,
                              float rotaryStartAngle,
                              float rotaryEndAngle,
                              juce::Slider& slider) override;
        juce::Font getComboBoxFont(juce::ComboBox&) override;
        juce::Font getPopupMenuFont() override;
        juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    };

    struct KnobControl
    {
        juce::Slider slider;
        juce::Label titleLabel;
        juce::Label valueLabel;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void setupKnob(KnobControl& kc,
                   const juce::String& paramId,
                   const juce::String& title,
                   std::function<juce::String(double)> formatter);
    void updateKnobValueLabel(KnobControl& kc, const std::function<juce::String(double)>& formatter);
    void timerCallback() override;
    void chooseSamplesFolder();

    ICStandardGuitarProcessor& audioProcessor;
    GuitarLookAndFeel customLookAndFeel;

    juce::ComboBox articulationBox;
    juce::Label articulationLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> articulationAttachment;
    juce::TextButton cc80InvertButton { juce::CharPointer_UTF8("⇄ CC#80極性反転") };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> cc80InvertAttachment;

    KnobControl attackTrimKnob;
    KnobControl releaseTimeKnob;
    KnobControl damperSoftnessKnob;
    KnobControl releaseNoiseKnob;
    KnobControl toneEqKnob;
    KnobControl ambienceKnob;
    KnobControl outputGainKnob;

    juce::TextButton chooseFolderButton { juce::CharPointer_UTF8("フォルダ変更...") };
    juce::TextButton auditionDownButton { juce::CharPointer_UTF8("♪ Cコード (Down)") };
    juce::TextButton auditionUpButton { juce::CharPointer_UTF8("♪ Cコード (Up)") };
    juce::TextButton auditionMuteButton { juce::CharPointer_UTF8("♪ パームミュート試奏") };
    juce::TextButton auditionReleaseButton { juce::CharPointer_UTF8("■ ミュート (Key-Off)") };

    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ICStandardGuitarEditor)
};
