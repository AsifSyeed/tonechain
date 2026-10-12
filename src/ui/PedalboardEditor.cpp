#include "PedalboardEditor.h"
#include "Theme.h"

namespace theme = pedalboard::theme;

namespace
{
constexpr int headerHeight = 56;
constexpr int scrollBarThickness = 8;
constexpr int detailHeaderHeight = 44;
constexpr int emptyDetailHeight = 150;
// Room left for the host's own plug-in window chrome (Logic's title bar and
// preset/compare toolbar) so the whole window stays on screen.
constexpr int hostChromeHeight = 130;
constexpr int screenMargin = 24;
constexpr int minWidth = 900;
constexpr int padding = 16;
constexpr int manageAudioUnitsMenuId = 0x7fff0001;
constexpr int scanAudioUnitsMenuId = 0x7fff0002;

void styleButton (juce::TextButton& b, juce::Colour fill, juce::Colour text)
{
    b.setColour (juce::TextButton::buttonColourId, fill);
    b.setColour (juce::TextButton::buttonOnColourId, fill.brighter (0.2f));
    b.setColour (juce::TextButton::textColourOffId, text);
    b.setColour (juce::TextButton::textColourOnId, text);
    b.setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
}

juce::AudioPluginFormat* findAudioUnitFormat (juce::AudioPluginFormatManager& manager)
{
    for (auto* format : manager.getFormats())
        if (format->getName() == "AudioUnit")
            return format;

    return nullptr;
}
} // namespace

//==============================================================================
void PedalRow::setPedals (std::vector<std::unique_ptr<PedalComponent>> newPedals, int numLoading)
{
    if (isDragging())
    {
        deferredUpdate.emplace (std::move (newPedals), numLoading);
        return;
    }

    for (auto& p : pedals)
        removeChildComponent (p.get());

    pedals = std::move (newPedals);
    loading = numLoading;

    for (size_t i = 0; i < pedals.size(); ++i)
    {
        auto& pedal = *pedals[i];
        const auto index = (int) i;

        pedal.onDragStart = [this, index] (const juce::MouseEvent& e) { startDrag (index, e); };
        pedal.onDragMove = [this] (const juce::MouseEvent& e) { dragTo (e); };
        pedal.onDragEnd = [this] (const juce::MouseEvent&) { endDrag(); };
        addAndMakeVisible (pedal);
    }

    resized();
    repaint();
}

int PedalRow::slotX (int slot) const
{
    return jackMargin + slot * (PedalComponent::width + gap);
}

juce::Rectangle<int> PedalRow::slotBounds (int slot) const
{
    return { slotX (slot), top, PedalComponent::width, PedalComponent::height };
}

int PedalRow::getPreferredWidth() const
{
    const auto numSlots = (int) pedals.size() + loading + 1; // +1 for the "add" slot
    return slotX (numSlots) - gap + jackMargin;
}

juce::Rectangle<int> PedalRow::getAddSlotArea() const
{
    return { slotX ((int) pedals.size() + loading), top, PedalComponent::width, PedalComponent::height };
}

void PedalRow::resized()
{
    if (isDragging())
    {
        layoutForDrag();
        return;
    }

    for (size_t i = 0; i < pedals.size(); ++i)
        pedals[i]->setBounds (slotBounds ((int) i));
}

void PedalRow::startDrag (int index, const juce::MouseEvent& e)
{
    if (! juce::isPositiveAndBelow (index, (int) pedals.size()))
        return;

    dragIndex = index;
    dragSlot = index;
    grabOffsetX = e.getMouseDownX();
    pedals[(size_t) index]->toFront (false);
    repaint();
}

void PedalRow::dragTo (const juce::MouseEvent& e)
{
    if (! isDragging())
        return;

    const auto numPedals = (int) pedals.size();
    const auto pitch = PedalComponent::width + gap;
    const auto mouse = e.getEventRelativeTo (this).getPosition();
    const auto x = juce::jlimit (slotX (0) - gap, slotX (numPedals - 1) + gap, mouse.x - grabOffsetX);

    // The held pedal follows the mouse, lifted slightly off the board.
    pedals[(size_t) dragIndex]->setTopLeftPosition (x, top - 8);

    const auto slot = juce::jlimit (0, numPedals - 1, juce::roundToInt ((float) (x - jackMargin) / (float) pitch));

    if (slot != dragSlot)
    {
        dragSlot = slot;
        layoutForDrag();
        repaint();
    }

    // Long boards scroll when the pedal is dragged near either edge.
    if (auto* viewport = findParentComponentOfClass<juce::Viewport>())
    {
        const auto inViewport = e.getEventRelativeTo (viewport).getPosition();
        viewport->autoScroll (inViewport.x, inViewport.y, 48, 14);
    }
}

void PedalRow::layoutForDrag()
{
    // Order the pedals as they'd be after the drop and slide the others there.
    std::vector<int> order;

    for (int i = 0; i < (int) pedals.size(); ++i)
        if (i != dragIndex)
            order.push_back (i);

    order.insert (order.begin() + dragSlot, dragIndex);

    auto& animator = juce::Desktop::getInstance().getAnimator();

    for (size_t slot = 0; slot < order.size(); ++slot)
        if (order[slot] != dragIndex)
            animator.animateComponent (pedals[(size_t) order[slot]].get(), slotBounds ((int) slot), 1.0f, 140, false, 1.0, 1.0);
}

void PedalRow::endDrag()
{
    if (! isDragging())
        return;

    const auto from = std::exchange (dragIndex, -1);
    const auto to = std::exchange (dragSlot, -1);

    juce::Desktop::getInstance().getAnimator().animateComponent (pedals[(size_t) from].get(), slotBounds (to),
                                                                  1.0f, 120, false, 1.0, 1.0);

    if (deferredUpdate.has_value())
    {
        auto update = std::move (*deferredUpdate);
        deferredUpdate.reset();
        setPedals (std::move (update.first), update.second);
    }

    repaint();

    if (from != to && onMove != nullptr)
        onMove (from, to);
}

void PedalRow::drawCable (juce::Graphics& g, juce::Point<float> from, juce::Point<float> to) const
{
    const auto sag = juce::jlimit (10.0f, 60.0f, from.getDistanceFrom (to) * 0.18f);

    juce::Path cable;
    cable.startNewSubPath (from);
    cable.cubicTo (from.translated (sag, sag), to.translated (-sag, sag), to);

    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.strokePath (cable, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                  juce::AffineTransform::translation (0.0f, 4.0f));
    g.setColour (theme::cable);
    g.strokePath (cable, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (theme::cableHighlight);
    g.strokePath (cable, juce::PathStrokeType (1.5f), juce::AffineTransform::translation (0.0f, -1.5f));
}

void PedalRow::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    // Board surface.
    g.setGradientFill (juce::ColourGradient (theme::boardTop, 0.0f, 0.0f, theme::boardBottom, 0.0f, bounds.getBottom(), false));
    g.fillRect (bounds);

    g.setColour (theme::boardStripe);
    for (auto y = 14.0f; y < bounds.getBottom(); y += 28.0f)
        g.fillRect (0.0f, y, bounds.getWidth(), 10.0f);

    // IN / OUT jacks and the cables between every item on the board.
    const auto jackY = (float) top + PedalComponent::inputJack().y;
    const juce::Point<float> inJack { 22.0f, jackY };
    const juce::Point<float> outJack { bounds.getRight() - 22.0f, jackY };

    std::vector<juce::Rectangle<float>> items;
    const auto numItems = (int) pedals.size() + loading;

    for (int i = 0; i < numItems; ++i)
        items.push_back (juce::Rectangle<int> (slotX (i), top, PedalComponent::width, PedalComponent::height).toFloat());

    auto from = inJack;

    for (const auto& item : items)
    {
        drawCable (g, from, item.getTopLeft() + PedalComponent::inputJack());
        from = item.getTopLeft() + PedalComponent::outputJack();
    }

    drawCable (g, from, outJack);

    for (const auto& [jack, text] : { std::pair { inJack, "IN" }, std::pair { outJack, "OUT" } })
    {
        g.setColour (theme::jackMetal);
        g.fillEllipse (juce::Rectangle<float> (18.0f, 18.0f).withCentre (jack));
        g.setColour (juce::Colours::black);
        g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (jack));
        g.setColour (theme::textSecondary);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (text, juce::Rectangle<float> (44.0f, 16.0f).withCentre (jack.translated (0.0f, -22.0f)),
                    juce::Justification::centred);
    }

    // Where the held pedal will land.
    if (isDragging())
    {
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.fillRoundedRectangle (slotBounds (dragSlot).toFloat().reduced (6.0f, 4.0f), 10.0f);
    }

    // Placeholders for pedals that are still being instantiated.
    for (int i = (int) pedals.size(); i < numItems; ++i)
    {
        const auto r = items[(size_t) i].reduced (6.0f, 4.0f);
        g.setColour (theme::panel.withAlpha (0.85f));
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (theme::textSecondary);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText ("Loading...", r, juce::Justification::centred);
    }

    // "Add pedal" slot.
    const auto add = getAddSlotArea().toFloat().reduced (6.0f, 4.0f);
    g.setColour (theme::boardBottom.withAlpha (0.9f));
    g.fillRoundedRectangle (add, 10.0f);

    juce::Path outline;
    outline.addRoundedRectangle (add, 10.0f);
    const float dashes[] { 6.0f, 5.0f };
    juce::Path dashed;
    juce::PathStrokeType (1.5f).createDashedStroke (dashed, outline, dashes, 2);
    g.setColour (hoveringAddSlot ? theme::accent : theme::textSecondary.withAlpha (0.6f));
    g.fillPath (dashed);

    g.setFont (juce::FontOptions (34.0f));
    g.drawText ("+", add.withTrimmedBottom (30.0f), juce::Justification::centred);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("Add pedal", add.withTrimmedTop (60.0f), juce::Justification::centred);
}

void PedalRow::mouseMove (const juce::MouseEvent& e)
{
    const auto over = getAddSlotArea().contains (e.getPosition());

    if (over != hoveringAddSlot)
    {
        hoveringAddSlot = over;
        setMouseCursor (over ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint (getAddSlotArea());
    }
}

void PedalRow::mouseUp (const juce::MouseEvent& e)
{
    if (getAddSlotArea().contains (e.getPosition()) && onAddClicked)
        onAddClicked();
}

//==============================================================================
class PedalboardEditor::PluginManagerOverlay : public juce::Component
{
public:
    explicit PluginManagerOverlay (PluginLibrary& library)
        : list (library.formatManager, library.knownPlugins, library.getDeadMansPedalFile(),
                &library.getSettings(), true)
    {
        title.setText ("Audio Units", juce::dontSendNotification);
        title.setFont (juce::FontOptions (18.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, theme::textPrimary);

        hint.setText ("Use Options > \"Scan for new or updated AudioUnit plug-ins\" to find what's installed. "
                      "Only effects show up in the Add Pedal menu.",
                      juce::dontSendNotification);
        hint.setColour (juce::Label::textColourId, theme::textSecondary);

        styleButton (doneButton, theme::accent, juce::Colours::black);
        doneButton.onClick = [this] { if (onDone) onDone(); };

        addAndMakeVisible (title);
        addAndMakeVisible (hint);
        addAndMakeVisible (list);
        addAndMakeVisible (doneButton);
    }

    void scanAudioUnits (PluginLibrary& library)
    {
        if (auto* format = findAudioUnitFormat (library.formatManager))
            list.scanFor (*format);
    }

    std::function<void()> onDone;

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black.withAlpha (0.65f));
        g.setColour (theme::panel);
        g.fillRoundedRectangle (panelArea().toFloat(), 10.0f);
        g.setColour (theme::panelBorder);
        g.drawRoundedRectangle (panelArea().toFloat(), 10.0f, 1.0f);
    }

    void resized() override
    {
        auto r = panelArea().reduced (padding);
        auto top = r.removeFromTop (30);
        doneButton.setBounds (top.removeFromRight (90));
        title.setBounds (top);
        hint.setBounds (r.removeFromTop (34));
        r.removeFromTop (6);
        list.setBounds (r);
    }

    // Swallow clicks so the board underneath can't be used while open.
    void mouseDown (const juce::MouseEvent&) override {}

private:
    juce::Rectangle<int> panelArea() const
    {
        return getLocalBounds().reduced (juce::jmax (padding, (getWidth() - 820) / 2), padding);
    }

    juce::Label title, hint;
    juce::TextButton doneButton { "Done" };
    juce::PluginListComponent list;
};

//==============================================================================
// A pedal's own UI shown in a separate window, for UIs too big to embed.
class PedalboardEditor::PedalWindow : public juce::DocumentWindow
{
public:
    PedalWindow (const juce::String& name, juce::Component& content, bool keepOnTop, std::function<void()> onCloseIn)
        : DocumentWindow (name, theme::background, closeButton), onClose (std::move (onCloseIn))
    {
        setUsingNativeTitleBar (true);
        setContentNonOwned (&content, true);
        setAlwaysOnTop (keepOnTop);
    }

    void closeButtonPressed() override
    {
        // Our owner deletes us, so do it once this callback has returned.
        juce::MessageManager::callAsync ([cb = onClose] { cb(); });
    }

private:
    std::function<void()> onClose;
};

//==============================================================================
PedalboardEditor::PedalboardEditor (PedalboardProcessor& p)
    : AudioProcessorEditor (p), board (p)
{
    setOpaque (true);

    styleButton (addButton, theme::accent, juce::Colours::black);
    styleButton (pluginsButton, theme::panel.brighter (0.15f), theme::textPrimary);
    styleButton (midiButton, theme::panel.brighter (0.15f), theme::midi.brighter (0.3f));
    styleButton (detailBypassButton, theme::panel.brighter (0.15f), theme::textPrimary);
    styleButton (detailWindowButton, theme::panel.brighter (0.15f), theme::textPrimary);

    addButton.onClick = [this] { showAddMenu(); };
    pluginsButton.onClick = [this] { showPluginManager (false); };
    midiButton.onClick = [this] { showMidiInputMenu(); };
    midiButton.setTooltip ("Which MIDI devices can switch pedals");

    statusLabel.setJustificationType (juce::Justification::centredRight);
    statusLabel.setFont (juce::FontOptions (13.0f));

    detailTitle.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    detailTitle.setColour (juce::Label::textColourId, theme::textPrimary);

    detailBypassButton.onClick = [this]
    {
        if (const auto* pedal = findPedal (selectedNode))
            board.setPedalBypassed (selectedNode, ! pedal->bypassed);
    };

    detailWindowButton.onClick = [this]
    {
        if (hostedWindow != nullptr)
        {
            selectPedal (selectedNode, false);

            if (hostedWindow != nullptr)
                setStatus ("That pedal's controls are too big to fit here, so they stay in their own window.");
        }
        else
        {
            selectPedal (selectedNode, true);
        }
    };

    row.onMove = [this] (int from, int to) { board.movePedal (from, to); };
    row.onAddClicked = [this] { showAddMenu(); };

    boardViewport.setViewedComponent (&row, false);
    boardViewport.setScrollBarsShown (false, true);
    boardViewport.setScrollBarThickness (scrollBarThickness);
    boardViewport.getHorizontalScrollBar().setColour (juce::ScrollBar::thumbColourId, theme::panelBorder.brighter (0.4f));
    boardViewport.getHorizontalScrollBar().setColour (juce::ScrollBar::trackColourId, theme::background);

    for (auto* c : std::initializer_list<juce::Component*> { &addButton, &pluginsButton, &midiButton, &statusLabel,
                                                              &boardViewport, &detailTitle, &detailBypassButton, &detailWindowButton })
        addAndMakeVisible (c);

    board.addPedalListener (this);
    rebuildBoard();
    updateSize();
}

PedalboardEditor::~PedalboardEditor()
{
    board.removePedalListener (this);
    closeHostedEditor();
}

//==============================================================================
void PedalboardEditor::pedalWillBeRemoved (PedalboardProcessor::NodeID nodeId)
{
    if (nodeId == selectedNode)
    {
        closeHostedEditor();
        updateSize();
    }
}

void PedalboardEditor::pedalAdded (PedalboardProcessor::NodeID nodeId, bool addedByUser)
{
    if (addedByUser)
    {
        pendingSelect = nodeId;
        triggerAsyncUpdate();
    }
}

void PedalboardEditor::midiLearned (PedalboardProcessor::NodeID nodeId, const MidiTrigger& trigger)
{
    const auto* pedal = findPedal (nodeId);
    const auto name = pedal != nullptr ? pedal->description.name : juce::String ("Pedal");
    setStatus (name + " is now switched by MIDI " + trigger.describe(), false, true);
}

void PedalboardEditor::pedalsChanged()
{
    // Rebuild asynchronously: this can be triggered from inside a pedal's own
    // mouse callback, and rebuilding deletes that pedal component.
    triggerAsyncUpdate();
}

void PedalboardEditor::loadFailed (const juce::String& pluginName, const juce::String& error)
{
    setStatus ("Couldn't load " + pluginName + (error.isNotEmpty() ? ": " + error : juce::String()), true);
}

void PedalboardEditor::handleAsyncUpdate()
{
    rebuildBoard();
}

const PedalboardProcessor::Pedal* PedalboardEditor::findPedal (PedalboardProcessor::NodeID nodeId) const
{
    for (const auto& pedal : pedals)
        if (pedal.nodeId == nodeId)
            return &pedal;

    return nullptr;
}

void PedalboardEditor::rebuildBoard()
{
    pedals = board.getPedals();
    numLoading = board.getNumPendingLoads();

    if (selectedNode != PedalboardProcessor::NodeID{} && findPedal (selectedNode) == nullptr)
        closeHostedEditor();

    if (pendingSelect != PedalboardProcessor::NodeID{})
    {
        const auto toSelect = std::exchange (pendingSelect, PedalboardProcessor::NodeID{});

        if (findPedal (toSelect) != nullptr)
        {
            selectPedal (toSelect); // calls back into rebuildBoard()
            return;
        }
    }

    const auto learnTarget = board.getMidiLearnTarget();
    std::vector<std::unique_ptr<PedalComponent>> components;
    auto previousColour = -1;

    for (size_t i = 0; i < pedals.size(); ++i)
    {
        const auto nodeId = pedals[i].nodeId;
        // Stable colour per plug-in, nudged along so neighbours never match.
        auto colour = theme::enclosureColourIndexFor (pedals[i].description.name);

        if (colour == previousColour)
            colour = (colour + 1) % (int) std::size (theme::enclosurePalette);

        previousColour = colour;

        auto pedal = std::make_unique<PedalComponent> ((int) i, pedals[i], theme::enclosurePalette[colour],
                                                       nodeId == selectedNode, nodeId == learnTarget);

        pedal->onSelect = [this, nodeId] { selectPedal (nodeId); };
        pedal->onRemove = [this, nodeId] { board.removePedal (nodeId); };

        if (i > 0)
            pedal->onMoveLeft = [this, i] { board.movePedal ((int) i, (int) i - 1); };

        if (i + 1 < pedals.size())
            pedal->onMoveRight = [this, i] { board.movePedal ((int) i, (int) i + 1); };
        pedal->onToggleBypass = [this, nodeId]
        {
            if (const auto* p = findPedal (nodeId))
                board.setPedalBypassed (nodeId, ! p->bypassed);
        };
        pedal->onStartMidiLearn = [this, nodeId] { keepStatus = false; board.startMidiLearn (nodeId); };
        pedal->onCancelMidiLearn = [this] { board.cancelMidiLearn(); };
        pedal->onClearMidi = [this, nodeId] { board.setPedalMidiTrigger (nodeId, {}); };
        pedal->onSetMidiMode = [this, nodeId] (MidiTrigger::Mode mode)
        {
            if (const auto* p = findPedal (nodeId))
            {
                auto trigger = p->midi;
                trigger.mode = mode;
                board.setPedalMidiTrigger (nodeId, trigger);
            }
        };

        components.push_back (std::move (pedal));
    }

    row.setPedals (std::move (components), numLoading);

    if (const auto* learning = findPedal (learnTarget))
        setStatus ("MIDI Learn: press a footswitch for " + learning->description.name + " (right-click it to cancel)");
    else if (numLoading > 0)
        setStatus ("Loading " + juce::String (numLoading) + (numLoading == 1 ? " pedal..." : " pedals..."));
    else if (! keepStatus)
        setStatus ({});

    updateMidiButton();

    // Detail header for the selected pedal.
    const auto selected = std::find_if (pedals.begin(), pedals.end(),
                                        [this] (const auto& p) { return p.nodeId == selectedNode; });
    const auto hasSelection = selected != pedals.end() && hostedEditor != nullptr;

    detailTitle.setVisible (hasSelection);
    detailBypassButton.setVisible (hasSelection);
    detailWindowButton.setVisible (hasSelection);
    detailWindowButton.setButtonText (hostedWindow != nullptr ? "Dock" : "Pop Out");

    if (hasSelection)
    {
        detailTitle.setText (selected->description.name + "  -  " + selected->description.manufacturerName,
                             juce::dontSendNotification);
        detailBypassButton.setButtonText (selected->bypassed ? "Turn On" : "Bypass");
    }

    updateSize(); // the board may have grown or shrunk
    repaint();
}

//==============================================================================
void PedalboardEditor::selectPedal (PedalboardProcessor::NodeID nodeId, bool inWindow)
{
    keepStatus = false;

    if (const auto* pedal = findPedal (nodeId); pedal != nullptr && pedal->missing)
    {
        closeHostedEditor();
        setStatus (pedal->description.name + " isn't available: " + pedal->missingReason
                       + ". It stays on the board with its settings.",
                   true);
        rebuildBoard();
        updateSize();
        return;
    }

    const auto alreadyShowing = nodeId == selectedNode && hostedEditor != nullptr
                                && inWindow == (hostedWindow != nullptr);

    if (alreadyShowing)
    {
        if (hostedWindow != nullptr)
            hostedWindow->toFront (true);
    }
    else
    {
        closeHostedEditor();

        if (auto* instance = board.getPedalInstance (nodeId))
        {
            juce::AudioProcessorEditor* editor = instance->hasEditor() ? instance->createEditorIfNeeded() : nullptr;

            if (editor == nullptr)
                editor = new juce::GenericAudioProcessorEditor (*instance);

            selectedNode = nodeId;
            hostedEditor.reset (editor);

            if (inWindow || ! fitHostedEditorInDetailArea())
            {
                openHostedEditorWindow();
            }
            else
            {
                addAndMakeVisible (*hostedEditor);
                hostedEditor->addComponentListener (this);
            }
        }
    }

    rebuildBoard();
    updateSize();
}

void PedalboardEditor::openHostedEditorWindow()
{
    const auto* pedal = findPedal (selectedNode);
    const auto name = pedal != nullptr ? pedal->description.name : juce::String ("Pedal");

    // Inside a DAW, keep the window above the host's windows like its own plug-in windows.
    const auto keepOnTop = board.wrapperType != juce::AudioProcessor::wrapperType_Standalone;

    hostedWindow = std::make_unique<PedalWindow> (name, *hostedEditor, keepOnTop,
                                                  [safe = SafePointer (this)]
                                                  {
                                                      if (safe != nullptr)
                                                      {
                                                          safe->closeHostedEditor();
                                                          safe->rebuildBoard();
                                                          safe->updateSize();
                                                      }
                                                  });

    hostedWindow->centreAroundComponent (this, hostedWindow->getWidth(), hostedWindow->getHeight());

    // Never let the window open off screen.
    if (auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (hostedWindow->getBounds()))
        hostedWindow->setBounds (hostedWindow->getBounds().constrainedWithin (display->userArea));

    hostedWindow->setVisible (true);
    hostedWindow->toFront (true);
}

void PedalboardEditor::closeHostedEditor()
{
    if (hostedWindow != nullptr)
    {
        hostedWindow->clearContentComponent();
        hostedWindow.reset();
    }

    if (hostedEditor != nullptr)
    {
        hostedEditor->removeComponentListener (this);
        removeChildComponent (hostedEditor.get());
        hostedEditor.reset();
    }

    selectedNode = {};
}

juce::Rectangle<int> PedalboardEditor::getMaxEditorSize() const
{
    const auto& displays = juce::Desktop::getInstance().getDisplays();
    const auto* display = isShowing() ? displays.getDisplayForRect (getScreenBounds()) : displays.getPrimaryDisplay();

    if (display == nullptr)
        return { 1280, 800 };

    return { display->userArea.getWidth() - 2 * screenMargin,
             display->userArea.getHeight() - hostChromeHeight - screenMargin };
}

bool PedalboardEditor::fitHostedEditorInDetailArea()
{
    const auto maxSize = getMaxEditorSize();
    const auto availableWidth = maxSize.getWidth() - 2 * padding;
    const auto availableHeight = maxSize.getHeight() - (headerHeight + PedalRow::height + scrollBarThickness
                                                        + detailHeaderHeight + padding);

    const auto fits = [&] { return hostedEditor->getWidth() <= availableWidth && hostedEditor->getHeight() <= availableHeight; };

    if (fits())
        return true;

    // Resizable UIs get shrunk to fit, keeping their proportions.
    if (hostedEditor->isResizable() && hostedEditor->getWidth() > 0 && hostedEditor->getHeight() > 0)
    {
        const auto scale = juce::jmin ((double) availableWidth / hostedEditor->getWidth(),
                                       (double) availableHeight / hostedEditor->getHeight());
        hostedEditor->setSize (juce::roundToInt (hostedEditor->getWidth() * scale),
                               juce::roundToInt (hostedEditor->getHeight() * scale));
    }

    return fits();
}

void PedalboardEditor::componentMovedOrResized (juce::Component& c, bool, bool wasResized)
{
    if (! wasResized || &c != hostedEditor.get() || hostedWindow != nullptr)
        return;

    // An embedded pedal UI grew past what fits on screen: move it to a window.
    if (! fitHostedEditorInDetailArea())
    {
        juce::MessageManager::callAsync ([safe = SafePointer (this)]
        {
            if (safe != nullptr && safe->hostedEditor != nullptr && safe->hostedWindow == nullptr)
                safe->selectPedal (safe->selectedNode, true);
        });
        return;
    }

    updateSize();
}

void PedalboardEditor::updateSize()
{
    const auto maxSize = getMaxEditorSize();
    auto width = juce::jmax (minWidth, row.getPreferredWidth()); // show the whole board when it fits on screen
    auto height = headerHeight + PedalRow::height + scrollBarThickness + detailHeaderHeight;

    if (hostedEditor != nullptr && hostedWindow == nullptr)
    {
        width = juce::jmax (width, hostedEditor->getWidth() + 2 * padding);
        height += hostedEditor->getHeight() + padding;
    }
    else
    {
        height += emptyDetailHeight;
    }

    if (pluginManager != nullptr)
        height = juce::jmax (height, 600);

    width = juce::jmin (width, maxSize.getWidth());
    height = juce::jmin (height, maxSize.getHeight());

    if (getWidth() == width && getHeight() == height)
        resized();
    else
        setSize (width, height);
}

//==============================================================================
void PedalboardEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    auto header = getLocalBounds().removeFromTop (headerHeight);
    g.setColour (theme::panel);
    g.fillRect (header);
    g.setColour (theme::panelBorder);
    g.fillRect (header.removeFromBottom (1));

    g.setColour (theme::textPrimary);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("TONECHAIN", juce::Rectangle<int> (padding, 0, 200, headerHeight), juce::Justification::centredLeft);

    if (hostedEditor == nullptr || hostedWindow != nullptr)
    {
        const auto detail = getLocalBounds().withTrimmedTop (headerHeight + PedalRow::height + scrollBarThickness);
        const auto empty = pedals.empty() && numLoading == 0;

        g.setColour (theme::textSecondary);
        g.setFont (juce::FontOptions (15.0f));
        const auto message = hostedWindow != nullptr ? "This pedal's controls are open in their own window. Click the pedal to bring it to the front."
                           : empty                   ? "Your board is empty. Click \"+ Add Pedal\" to load an Audio Unit as a pedal."
                                                     : "Click a pedal to show its controls here. Drag pedals to reorder the chain.";

        g.drawFittedText (message,
                          detail.withTrimmedTop (detailHeaderHeight).reduced (padding), juce::Justification::centred, 2);
    }
}

void PedalboardEditor::resized()
{
    auto r = getLocalBounds();

    auto header = r.removeFromTop (headerHeight).reduced (padding, 12);
    addButton.setBounds (header.removeFromRight (120));
    header.removeFromRight (8);
    pluginsButton.setBounds (header.removeFromRight (120));
    header.removeFromRight (8);
    midiButton.setBounds (header.removeFromRight (180));
    header.removeFromRight (12);
    statusLabel.setBounds (header.withTrimmedLeft (180));

    boardViewport.setBounds (r.removeFromTop (PedalRow::height + scrollBarThickness));
    row.setSize (juce::jmax (row.getPreferredWidth(), boardViewport.getWidth()), PedalRow::height);

    auto detailHeader = r.removeFromTop (detailHeaderHeight).reduced (padding, 8);
    detailBypassButton.setBounds (detailHeader.removeFromRight (90));
    detailHeader.removeFromRight (8);
    detailWindowButton.setBounds (detailHeader.removeFromRight (90));
    detailTitle.setBounds (detailHeader);

    if (hostedEditor != nullptr && hostedWindow == nullptr)
        hostedEditor->setTopLeftPosition (r.getX() + (r.getWidth() - hostedEditor->getWidth()) / 2, r.getY());

    if (pluginManager != nullptr)
        pluginManager->setBounds (getLocalBounds());
}

//==============================================================================
void PedalboardEditor::showAddMenu()
{
    board.getLibrary().reloadIfChangedOnDisk();
    const auto effects = board.getLibrary().getEffects();
    juce::PopupMenu menu;

    if (effects.isEmpty())
    {
        menu.addItem (scanAudioUnitsMenuId, "Scan for Audio Units...");
    }
    else
    {
        juce::KnownPluginList::addToMenu (menu, effects, juce::KnownPluginList::sortByManufacturer);
        menu.addSeparator();
        menu.addItem (manageAudioUnitsMenuId, "Manage Audio Units...");
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addButton),
                        [safe = SafePointer (this), effects] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;

                            if (result == scanAudioUnitsMenuId)
                                return safe->showPluginManager (true);

                            if (result == manageAudioUnitsMenuId)
                                return safe->showPluginManager (false);

                            const auto index = juce::KnownPluginList::getIndexChosenByMenu (effects, result);

                            if (juce::isPositiveAndBelow (index, effects.size()))
                            {
                                safe->keepStatus = false;
                                safe->board.addPedal (effects.getReference (index));
                            }
                        });
}

void PedalboardEditor::showPluginManager (bool startAudioUnitScan)
{
    if (pluginManager == nullptr)
    {
        board.getLibrary().reloadIfChangedOnDisk();
        pluginManager = std::make_unique<PluginManagerOverlay> (board.getLibrary());
        pluginManager->onDone = [this] { hidePluginManager(); };
        addAndMakeVisible (*pluginManager);
        updateSize();
    }

    if (startAudioUnitScan)
        pluginManager->scanAudioUnits (board.getLibrary());
}

void PedalboardEditor::hidePluginManager()
{
    // Called from the overlay's own button, so delete it after the callback returns.
    juce::MessageManager::callAsync ([safe = SafePointer (this)]
    {
        if (safe != nullptr)
        {
            safe->pluginManager.reset();
            safe->updateSize();
        }
    });
}

void PedalboardEditor::updateMidiButton()
{
    const auto& selection = board.getMidiInputSelection();
    juce::String text;

    if (selection == PedalboardProcessor::midiInputNone)
        text = "MIDI devices: Off";
    else if (selection == PedalboardProcessor::midiInputAll)
        text = "MIDI: All devices";
    else
    {
        text = "MIDI: (disconnected)";

        for (const auto& device : juce::MidiInput::getAvailableDevices())
            if (device.identifier == selection)
                text = "MIDI: " + device.name;
    }

    midiButton.setButtonText (text);
}

void PedalboardEditor::showMidiInputMenu()
{
    constexpr int offId = 1, allId = 2, firstDeviceId = 100;

    const auto devices = juce::MidiInput::getAvailableDevices();
    const auto& selection = board.getMidiInputSelection();

    juce::PopupMenu menu;
    menu.addSectionHeader ("Switch pedals from these MIDI devices");
    menu.addItem (allId, "All MIDI devices", true, selection == PedalboardProcessor::midiInputAll);

    for (int i = 0; i < devices.size(); ++i)
        menu.addItem (firstDeviceId + i, devices[i].name, true, devices[i].identifier == selection);

    menu.addItem (offId, "Off (only MIDI sent by the host)", true, selection == PedalboardProcessor::midiInputNone);
    menu.addSeparator();
    menu.addItem (-1, "Right-click a pedal > MIDI Learn to assign a footswitch", false, false);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&midiButton),
                        [safe = SafePointer (this), devices] (int result)
                        {
                            if (safe == nullptr || result <= 0)
                                return;

                            if (result == offId)
                                safe->board.setMidiInputSelection (PedalboardProcessor::midiInputNone);
                            else if (result == allId)
                                safe->board.setMidiInputSelection (PedalboardProcessor::midiInputAll);
                            else if (juce::isPositiveAndBelow (result - firstDeviceId, devices.size()))
                                safe->board.setMidiInputSelection (devices[result - firstDeviceId].identifier);
                        });
}

void PedalboardEditor::setStatus (const juce::String& text, bool isError, bool keep)
{
    keepStatus = isError || keep;
    statusLabel.setText (text, juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, isError ? theme::ledOn.brighter (0.3f) : theme::textSecondary);
}
