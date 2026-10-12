#include "PedalComponent.h"
#include "Theme.h"

namespace theme = pedalboard::theme;

PedalComponent::PedalComponent (int indexIn, const PedalboardProcessor::Pedal& pedalIn, bool isSelected, bool isLearningMidi)
    : index (indexIn), pedal (pedalIn), selected (isSelected), learningMidi (isLearningMidi)
{
    setSize (width, height);
    setRepaintsOnMouseActivity (true);
    setTitle (pedal.description.name);

    auto description = juce::String (pedal.missing ? "Not installed" : pedal.bypassed ? "Bypassed" : "Active");

    if (pedal.midi.isAssigned())
        description << ", switched by MIDI " << pedal.midi.describe();

    setDescription (description);
}

juce::Rectangle<float> PedalComponent::getBodyArea() const
{
    return getLocalBounds().toFloat().reduced (6.0f, 4.0f);
}

juce::Rectangle<float> PedalComponent::getFootswitchArea() const
{
    const auto body = getBodyArea();
    return juce::Rectangle<float> (38.0f, 38.0f).withCentre ({ body.getCentreX(), body.getBottom() - 34.0f });
}

juce::Rectangle<float> PedalComponent::getRemoveArea() const
{
    const auto body = getBodyArea();
    return { body.getRight() - 24.0f, body.getY() + 6.0f, 18.0f, 18.0f };
}

void PedalComponent::paint (juce::Graphics& g)
{
    const auto body = getBodyArea();
    auto colour = theme::enclosureColourFor (pedal.description.name);

    if (pedal.missing)
        colour = juce::Colour::fromRGB (70, 72, 78);
    else if (pedal.bypassed)
        colour = colour.withMultipliedSaturation (0.55f).withMultipliedBrightness (0.75f);

    // Jacks on the sides of the enclosure.
    g.setColour (theme::jackMetal.darker (0.4f));
    g.fillRoundedRectangle (juce::Rectangle<float> (10.0f, 16.0f).withCentre (inputJack().translated (4.0f, 0.0f)), 2.0f);
    g.fillRoundedRectangle (juce::Rectangle<float> (10.0f, 16.0f).withCentre (outputJack().translated (-4.0f, 0.0f)), 2.0f);

    // Drop shadow + enclosure; a lifted pedal casts a deeper shadow.
    juce::DropShadow (juce::Colours::black.withAlpha (dragStarted ? 0.8f : 0.55f), dragStarted ? 20 : 14, { 0, dragStarted ? 10 : 6 })
        .drawForRectangle (g, body.toNearestInt());

    g.setGradientFill (juce::ColourGradient (colour.brighter (0.25f), body.getTopLeft(),
                                             colour.darker (0.35f), body.getBottomLeft(), false));
    g.fillRoundedRectangle (body, 10.0f);

    g.setColour (juce::Colours::white.withAlpha (0.22f));
    g.drawRoundedRectangle (body.reduced (1.0f), 9.0f, 1.0f);

    if (selected || learningMidi)
    {
        g.setColour (learningMidi ? theme::midi : theme::accent);
        g.drawRoundedRectangle (body.expanded (3.0f), 12.0f, 2.0f);
    }

    const auto textColour = colour.getPerceivedBrightness() > 0.62f ? juce::Colour::fromRGB (24, 24, 26)
                                                                     : theme::textPrimary;

    // Position number.
    g.setColour (textColour.withAlpha (0.55f));
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (juce::String (index + 1), body.withHeight (26.0f).withTrimmedLeft (10.0f), juce::Justification::centredLeft);

    // Status LED.
    const auto led = juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ body.getCentreX(), body.getY() + 17.0f });

    const auto ledLit = ! pedal.bypassed && ! pedal.missing;

    if (ledLit)
    {
        g.setColour (theme::ledOn.withAlpha (0.35f));
        g.fillEllipse (led.expanded (5.0f));
    }

    g.setColour (ledLit ? theme::ledOn : theme::ledOff);
    g.fillEllipse (led);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawEllipse (led, 1.0f);

    // Name plate.
    auto label = body.reduced (10.0f, 0.0f).withTrimmedTop (32.0f).withHeight (78.0f);
    g.setColour (juce::Colours::black.withAlpha (0.18f));
    g.fillRoundedRectangle (label, 5.0f);

    label = label.reduced (6.0f, 6.0f);
    g.setColour (textColour);
    g.setFont (juce::FontOptions (14.5f, juce::Font::bold));
    g.drawFittedText (pedal.description.name.toUpperCase(), label.withTrimmedBottom (18.0f).toNearestInt(),
                      juce::Justification::centred, 3, 0.8f);

    g.setColour (pedal.missing ? theme::ledOn.brighter (0.4f) : textColour.withAlpha (0.65f));
    g.setFont (juce::FontOptions (10.5f, pedal.missing ? juce::Font::bold : juce::Font::plain));
    g.drawFittedText (pedal.missing ? juce::String ("NOT INSTALLED") : pedal.description.manufacturerName,
                      label.removeFromBottom (16.0f).toNearestInt(), juce::Justification::centred, 1, 0.8f);

    // MIDI badge: what switches this pedal, or that it's waiting to learn.
    if (learningMidi || pedal.midi.isAssigned())
    {
        const auto text = learningMidi ? juce::String ("MIDI LEARN...") : "MIDI " + pedal.midi.shortLabel();
        const auto badge = juce::Rectangle<float> (learningMidi ? 92.0f : 74.0f, 16.0f)
                               .withCentre ({ body.getCentreX(), body.getY() + 122.0f });

        g.setColour (learningMidi ? theme::midi : juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (badge, 8.0f);
        g.setColour (learningMidi ? juce::Colours::black : theme::midi.brighter (0.3f));
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawFittedText (text, badge.toNearestInt(), juce::Justification::centred, 1, 0.8f);
    }

    // Footswitch.
    const auto foot = getFootswitchArea();
    const auto pressed = pressedFootswitch && isMouseButtonDown();

    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (foot.translated (0.0f, 2.0f));
    g.setGradientFill (juce::ColourGradient (juce::Colour::fromRGB (232, 233, 236), foot.getTopLeft(),
                                             juce::Colour::fromRGB (112, 114, 120), foot.getBottomRight(), false));
    g.fillEllipse (pressed ? foot.reduced (2.0f) : foot);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawEllipse (foot.reduced (7.0f), 1.5f);

    // Remove button, shown on hover.
    if (hovering || isMouseOver (true))
    {
        const auto r = getRemoveArea();
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillEllipse (r);
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        const auto x = r.reduced (5.5f);
        g.drawLine ({ x.getTopLeft(), x.getBottomRight() }, 1.6f);
        g.drawLine ({ x.getTopRight(), x.getBottomLeft() }, 1.6f);
    }
}

void PedalComponent::mouseEnter (const juce::MouseEvent&) { hovering = true; repaint(); }
void PedalComponent::mouseExit (const juce::MouseEvent&)  { hovering = false; repaint(); }

void PedalComponent::mouseDown (const juce::MouseEvent& e)
{
    dragStarted = false;
    pressedFootswitch = getFootswitchArea().contains (e.position);

    if (e.mods.isPopupMenu())
        showContextMenu();

    repaint();
}

void PedalComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;

    if (! dragStarted)
    {
        if (e.getDistanceFromDragStart() < 6)
            return;

        dragStarted = true;
        pressedFootswitch = false;
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        repaint();

        if (onDragStart != nullptr)
            onDragStart (e);
    }

    if (onDragMove != nullptr)
        onDragMove (e);
}

void PedalComponent::mouseUp (const juce::MouseEvent& e)
{
    if (dragStarted)
    {
        dragStarted = false;
        setMouseCursor (juce::MouseCursor::NormalCursor);
        repaint();

        if (onDragEnd != nullptr)
            onDragEnd (e);

        return;
    }

    const auto wasClick = ! e.mods.isPopupMenu() && e.getDistanceFromDragStart() < 6;
    const auto onFootswitch = pressedFootswitch && getFootswitchArea().contains (e.position);
    pressedFootswitch = false;
    repaint();

    if (! wasClick)
        return;

    if (onFootswitch)
    {
        if (onToggleBypass) onToggleBypass();
    }
    else if (getRemoveArea().contains (e.position))
    {
        if (onRemove) onRemove();
    }
    else if (onSelect)
    {
        onSelect();
    }
}

void PedalComponent::showContextMenu()
{
    const auto call = [safe = SafePointer (this)] (std::function<void()> PedalComponent::* callback)
    {
        return [safe, callback]
        {
            if (safe != nullptr && (safe.getComponent()->*callback) != nullptr)
                (safe.getComponent()->*callback)();
        };
    };

    juce::PopupMenu menu;
    menu.addSectionHeader (pedal.description.name);

    if (! pedal.missing)
        menu.addItem ("Show controls", call (&PedalComponent::onSelect));

    menu.addItem (pedal.bypassed ? "Turn on" : "Bypass", call (&PedalComponent::onToggleBypass));
    menu.addItem ("Move Left", onMoveLeft != nullptr, false, call (&PedalComponent::onMoveLeft));
    menu.addItem ("Move Right", onMoveRight != nullptr, false, call (&PedalComponent::onMoveRight));
    menu.addSeparator();

    if (learningMidi)
        menu.addItem ("Cancel MIDI Learn", call (&PedalComponent::onCancelMidiLearn));
    else
        menu.addItem (pedal.midi.isAssigned() ? "MIDI Learn (replace " + pedal.midi.describe() + ")" : "MIDI Learn...",
                      call (&PedalComponent::onStartMidiLearn));

    if (pedal.midi.isAssigned())
    {
        juce::PopupMenu modes;
        const auto setMode = [safe = SafePointer (this)] (MidiTrigger::Mode mode)
        {
            return [safe, mode]
            {
                if (safe != nullptr && safe->onSetMidiMode != nullptr)
                    safe->onSetMidiMode (mode);
            };
        };

        modes.addItem ("Toggle on each press", true, pedal.midi.mode == MidiTrigger::Mode::toggle,
                       setMode (MidiTrigger::Mode::toggle));
        modes.addItem ("Follow switch (on at 64+, off below)", true, pedal.midi.mode == MidiTrigger::Mode::follow,
                       setMode (MidiTrigger::Mode::follow));
        menu.addSubMenu ("MIDI switch behaviour", modes);
        menu.addItem ("Clear MIDI (" + pedal.midi.describe() + ")", call (&PedalComponent::onClearMidi));
    }

    menu.addSeparator();
    menu.addItem ("Remove from board", call (&PedalComponent::onRemove));

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
}
