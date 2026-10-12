#pragma once

#include <juce_graphics/juce_graphics.h>

namespace pedalboard::theme
{
inline const juce::Colour background = juce::Colour::fromRGB (17, 18, 21);
inline const juce::Colour boardTop = juce::Colour::fromRGB (38, 36, 34);
inline const juce::Colour boardBottom = juce::Colour::fromRGB (24, 23, 22);
inline const juce::Colour boardStripe = juce::Colours::white.withAlpha (0.025f);
inline const juce::Colour panel = juce::Colour::fromRGB (26, 27, 31);
inline const juce::Colour panelBorder = juce::Colour::fromRGB (52, 54, 60);

inline const juce::Colour textPrimary = juce::Colour::fromRGB (236, 237, 240);
inline const juce::Colour textSecondary = juce::Colour::fromRGB (150, 154, 162);
inline const juce::Colour accent = juce::Colour::fromRGB (255, 186, 73);

inline const juce::Colour cable = juce::Colour::fromRGB (14, 14, 16);
inline const juce::Colour cableHighlight = juce::Colours::white.withAlpha (0.10f);
inline const juce::Colour jackMetal = juce::Colour::fromRGB (170, 172, 176);

inline const juce::Colour ledOn = juce::Colour::fromRGB (255, 64, 54);
inline const juce::Colour midi = juce::Colour::fromRGB (96, 210, 255);
inline const juce::Colour ledOff = juce::Colour::fromRGB (70, 18, 16);

// Pedal enclosure colours. Each plug-in gets a stable one derived from its
// name; the board nudges it along when two neighbours would match.
inline const juce::Colour enclosurePalette[] {
    juce::Colour::fromRGB (196, 64, 52),   // red
    juce::Colour::fromRGB (222, 140, 40),  // orange
    juce::Colour::fromRGB (214, 186, 52),  // yellow
    juce::Colour::fromRGB (72, 150, 88),   // green
    juce::Colour::fromRGB (52, 132, 176),  // blue
    juce::Colour::fromRGB (112, 82, 168),  // purple
    juce::Colour::fromRGB (186, 186, 190), // silver
    juce::Colour::fromRGB (58, 60, 66),    // black
    juce::Colour::fromRGB (206, 92, 130),  // pink
    juce::Colour::fromRGB (64, 160, 160),  // teal
};

inline int enclosureColourIndexFor (const juce::String& name)
{
    return (int) ((juce::uint32) name.hashCode() % (juce::uint32) std::size (enclosurePalette));
}

} // namespace pedalboard::theme
