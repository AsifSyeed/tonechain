#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

// A MIDI message assigned to a pedal's footswitch: a CC or a note on one
// channel. Learned from the controller, so the user never types numbers.
struct MidiTrigger
{
    enum class Type { none, controller, note };

    enum class Mode
    {
        toggle, // every press (value >= 64 / note-on) flips the pedal
        follow  // value >= 64 / note-on turns it on, < 64 / note-off turns it off
    };

    Type type = Type::none;
    int channel = 1; // 1-16
    int number = 0;  // CC or note number
    Mode mode = Mode::toggle;

    bool isAssigned() const noexcept { return type != Type::none; }

    bool matches (Type t, int ch, int num) const noexcept
    {
        return type != Type::none && type == t && channel == ch && number == num;
    }

    juce::String describe() const
    {
        if (type == Type::controller)
            return "CC " + juce::String (number) + " (ch " + juce::String (channel) + ")";

        if (type == Type::note)
            return juce::MidiMessage::getMidiNoteName (number, true, true, 3) + " (ch " + juce::String (channel) + ")";

        return {};
    }

    juce::String shortLabel() const
    {
        if (type == Type::controller)
            return "CC " + juce::String (number);

        if (type == Type::note)
            return juce::MidiMessage::getMidiNoteName (number, true, true, 3);

        return {};
    }

    void writeTo (juce::XmlElement& xml) const
    {
        if (! isAssigned())
            return;

        xml.setAttribute ("midiType", type == Type::controller ? "cc" : "note");
        xml.setAttribute ("midiChannel", channel);
        xml.setAttribute ("midiNumber", number);
        xml.setAttribute ("midiMode", mode == Mode::follow ? "follow" : "toggle");
    }

    static MidiTrigger readFrom (const juce::XmlElement& xml)
    {
        MidiTrigger t;
        const auto typeName = xml.getStringAttribute ("midiType");

        if (typeName != "cc" && typeName != "note")
            return t;

        t.type = typeName == "cc" ? Type::controller : Type::note;
        t.channel = juce::jlimit (1, 16, xml.getIntAttribute ("midiChannel", 1));
        t.number = juce::jlimit (0, 127, xml.getIntAttribute ("midiNumber"));
        t.mode = xml.getStringAttribute ("midiMode") == "follow" ? Mode::follow : Mode::toggle;
        return t;
    }
};
