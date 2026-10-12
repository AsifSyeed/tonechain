#pragma once

#include "PedalboardProcessor.h"
#include "PedalComponent.h"

#include <set>

// The board: a row of pedals joined by cables, between IN and OUT jacks.
// Pedals are reordered by dragging them along the row: the dragged pedal
// follows the mouse and the others slide aside to show where it will land.
class PedalRow : public juce::Component
{
public:
    static constexpr int height = 236;

    std::function<void (int fromIndex, int toIndex)> onMove;
    std::function<void()> onAddClicked;

    void setPedals (std::vector<std::unique_ptr<PedalComponent>>, int numLoading);
    int getPreferredWidth() const;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    static constexpr int jackMargin = 64;
    static constexpr int gap = 34;
    static constexpr int top = 18;

    int slotX (int slot) const;
    juce::Rectangle<int> slotBounds (int slot) const;
    bool isDragging() const noexcept { return dragIndex >= 0; }
    void startDrag (int index, const juce::MouseEvent&);
    void dragTo (const juce::MouseEvent&);
    void endDrag();
    void layoutForDrag();
    juce::Rectangle<int> getAddSlotArea() const;
    void drawCable (juce::Graphics&, juce::Point<float> from, juce::Point<float> to) const;

    std::vector<std::unique_ptr<PedalComponent>> pedals;
    int loading = 0;
    bool hoveringAddSlot = false;

    int dragIndex = -1;  // pedal being dragged, or -1
    int dragSlot = -1;   // where it would land if dropped now
    int grabOffsetX = 0; // where in the pedal the mouse grabbed it

    // A board update that arrived mid-drag; applied when the drag ends so the
    // pedal under the mouse isn't deleted while it's being held.
    std::optional<std::pair<std::vector<std::unique_ptr<PedalComponent>>, int>> deferredUpdate;
};

//==============================================================================
class PedalboardEditor : public juce::AudioProcessorEditor,
                         private PedalboardProcessor::Listener,
                         private juce::ComponentListener,
                         private juce::AsyncUpdater
{
public:
    explicit PedalboardEditor (PedalboardProcessor&);
    ~PedalboardEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class PluginManagerOverlay;
    class PedalWindow;

    // PedalboardProcessor::Listener
    void pedalWillBeRemoved (PedalboardProcessor::NodeID) override;
    void pedalAdded (PedalboardProcessor::NodeID, bool addedByUser) override;
    void pedalsChanged() override;
    void loadFailed (const juce::String& pluginName, const juce::String& error) override;
    void midiLearned (PedalboardProcessor::NodeID, const MidiTrigger&) override;

    void componentMovedOrResized (juce::Component&, bool wasMoved, bool wasResized) override;
    void handleAsyncUpdate() override;

    void rebuildBoard();
    void selectPedal (PedalboardProcessor::NodeID, bool inWindow = false);
    void closeHostedEditor();
    void openHostedEditorWindow();
    bool fitHostedEditorInDetailArea();
    juce::Rectangle<int> getMaxEditorSize() const;
    void updateSize();
    void showAddMenu();
    void showPluginManager (bool startAudioUnitScan);
    void hidePluginManager();
    void showMidiInputMenu();
    void updateMidiButton();
    const PedalboardProcessor::Pedal* findPedal (PedalboardProcessor::NodeID) const;

    // Errors and confirmations stay up until the user does something else;
    // other messages are replaced on the next update.
    void setStatus (const juce::String&, bool isError = false, bool keep = false);

    PedalboardProcessor& board;

    juce::TextButton addButton { "+ Add Pedal" };
    juce::TextButton pluginsButton { "Audio Units..." };
    juce::TextButton midiButton { "MIDI" };
    juce::Label statusLabel;

    juce::Viewport boardViewport;
    PedalRow row;

    juce::Label detailTitle;
    juce::TextButton detailBypassButton { "Bypass" };
    juce::TextButton detailWindowButton { "Pop Out" };

    // Snapshot of the chain taken on each rebuild, so painting and clicks
    // don't have to copy it from the processor.
    std::vector<PedalboardProcessor::Pedal> pedals;
    int numLoading = 0;

    PedalboardProcessor::NodeID selectedNode {};
    PedalboardProcessor::NodeID pendingSelect {}; // a pedal the user just added
    bool keepStatus = false;

    std::unique_ptr<PluginManagerOverlay> pluginManager;
    // Declared last so it is destroyed before anything it might refer to.
    // When a pedal's UI is too big to embed it is shown in hostedWindow instead.
    std::unique_ptr<juce::AudioProcessorEditor> hostedEditor;
    std::unique_ptr<PedalWindow> hostedWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedalboardEditor)
};
