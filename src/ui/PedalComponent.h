#pragma once

#include "PedalboardProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

// One stompbox on the board. Click to show its controls, click the footswitch
// to bypass, drag sideways to reorder, right-click for more (including MIDI Learn).
// Dragging itself is handled by the board through onDragStart/Move/End.
class PedalComponent : public juce::Component
{
public:
    static constexpr int width = 132;
    static constexpr int height = 196;

    PedalComponent (int index, const PedalboardProcessor::Pedal&, juce::Colour enclosure,
                    bool isSelected, bool isLearningMidi);

    // "ValhallaSupermassive" -> "Valhalla Supermassive", so long one-word
    // product names can wrap on the name plate instead of being cut off.
    static juce::String displayName (const juce::String& pluginName);

    int getIndex() const noexcept { return index; }

    std::function<void()> onSelect;
    std::function<void()> onToggleBypass;
    std::function<void()> onRemove;
    std::function<void()> onStartMidiLearn;
    std::function<void()> onCancelMidiLearn;
    std::function<void()> onClearMidi;
    std::function<void (MidiTrigger::Mode)> onSetMidiMode;
    std::function<void()> onMoveLeft;  // left unset when the pedal can't move that way
    std::function<void()> onMoveRight;

    std::function<void (const juce::MouseEvent&)> onDragStart;
    std::function<void (const juce::MouseEvent&)> onDragMove;
    std::function<void (const juce::MouseEvent&)> onDragEnd;

    // Where cables plug in, in this component's coordinates. Signal flows
    // left to right across the board.
    static juce::Point<float> inputJack()  { return { 0.0f, 46.0f }; }
    static juce::Point<float> outputJack() { return { (float) width, 46.0f }; }

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> getBodyArea() const;
    juce::Rectangle<float> getFootswitchArea() const;
    juce::Rectangle<float> getRemoveArea() const;
    void showContextMenu();

    const int index;
    const PedalboardProcessor::Pedal pedal;
    const juce::Colour enclosureColour;
    const bool selected;
    const bool learningMidi;
    bool hovering = false;
    bool dragStarted = false;
    bool pressedFootswitch = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedalComponent)
};
