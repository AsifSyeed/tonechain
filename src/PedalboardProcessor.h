#pragma once

#include "MidiTrigger.h"
#include "PluginLibrary.h"

#include <deque>
#include <map>
#include <optional>

// Hosts a serial chain of Audio Units ("pedals") inside an AudioProcessorGraph:
//
//   input -> pedal 1 -> pedal 2 -> ... -> output
//
// Bypassed pedals are wired around (true bypass) and switched into their cheap
// bypass mode. All chain editing happens on the message thread; the audio
// thread only ever touches the graph, which swaps in its new render sequence
// safely.
//
// Pedals can be switched on and off from MIDI (CCs or notes), either from the
// host's MIDI input or straight from the Mac's MIDI devices.
class PedalboardProcessor : public juce::AudioProcessor,
                            private juce::MidiInputCallback,
                            private juce::Timer
{
public:
    using NodeID = juce::AudioProcessorGraph::NodeID;

    struct Pedal
    {
        NodeID nodeId;
        juce::PluginDescription description;
        bool bypassed = false;
        MidiTrigger midi;

        // Set when the plugin couldn't be loaded (e.g. not installed on this
        // Mac). The pedal keeps its slot and its saved settings, so saving the
        // project never throws it away.
        bool missing = false;
        juce::String missingReason;
        juce::String missingState;
    };

    struct Listener
    {
        virtual ~Listener() = default;
        // Called synchronously before a pedal's plugin is destroyed, so any
        // open editor for it can be deleted first.
        virtual void pedalWillBeRemoved (NodeID) {}
        virtual void pedalAdded (NodeID, bool /*addedByUser*/) {}
        virtual void pedalsChanged() {}
        virtual void loadFailed (const juce::String& /*pluginName*/, const juce::String& /*error*/) {}
        virtual void midiLearned (NodeID, const MidiTrigger&) {}
    };

    // Values for setMidiInputSelection(), besides a device identifier.
    static inline const juce::String midiInputNone {};
    static inline const juce::String midiInputAll { "*all*" };

    PedalboardProcessor();
    ~PedalboardProcessor() override;

    //==============================================================================
    // Chain editing: message thread only.
    void addPedal (const juce::PluginDescription&);
    void removePedal (NodeID);
    void movePedal (int fromIndex, int toIndex);
    void setPedalBypassed (NodeID, bool shouldBeBypassed);
    void clearPedals();

    std::vector<Pedal> getPedals() const;
    juce::AudioPluginInstance* getPedalInstance (NodeID) const;
    int getNumPendingLoads() const;

    //==============================================================================
    // MIDI switching: message thread only.
    void startMidiLearn (NodeID);
    void cancelMidiLearn();
    NodeID getMidiLearnTarget() const noexcept { return midiLearnTarget; }
    void setPedalMidiTrigger (NodeID, const MidiTrigger&);

    void setMidiInputSelection (const juce::String&);
    const juce::String& getMidiInputSelection() const noexcept { return midiInputSelection; }

    //==============================================================================
    void addPedalListener (Listener* l)    { listeners.add (l); }
    void removePedalListener (Listener* l) { listeners.remove (l); }

    PluginLibrary& getLibrary() { return *library; }

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void reset() override;
    void setNonRealtime (bool isNonRealtime) noexcept override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return tailSeconds.load(); }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

private:
    struct PendingLoad
    {
        juce::PluginDescription description;
        juce::String base64State;
        bool bypassed = false;
        MidiTrigger midi;
        bool addedByUser = false;
    };

    // Raw 3-byte MIDI message, small enough to pass between threads by value.
    struct MidiEvent
    {
        juce::uint8 status = 0, data1 = 0, data2 = 0;
    };

    // Single-producer / single-consumer queue of MIDI events for the message thread.
    class MidiEventQueue
    {
    public:
        bool push (MidiEvent e) noexcept
        {
            const auto scope = fifo.write (1);

            if (scope.blockSize1 > 0)
            {
                events[(size_t) scope.startIndex1] = e;
                return true;
            }

            return false; // full: drop the event rather than block
        }

        template <typename Fn>
        void drain (Fn&& fn)
        {
            for (;;)
            {
                MidiEvent e;

                {
                    const auto scope = fifo.read (1);

                    if (scope.blockSize1 == 0)
                        return;

                    e = events[(size_t) scope.startIndex1];
                }

                fn (e);
            }
        }

    private:
        static constexpr int capacity = 256;
        juce::AbstractFifo fifo { capacity };
        std::array<MidiEvent, (size_t) capacity> events {};
    };

    void timerCallback() override;
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    void handleMidiEvent (MidiEvent);
    void openMidiInputs();
    void checkLatency();

    void loadNextInQueue();
    void installPedal (std::unique_ptr<juce::AudioPluginInstance>, const PendingLoad&);
    void installMissingPedal (const PendingLoad&, const juce::String& reason);
    void restoreFromXml (const juce::XmlElement&);
    void recreateIONodes();
    void rebuildConnections();
    void connectChannels (NodeID source, int numSourceChannels, NodeID dest, int numDestChannels);
    void applyBypassToNode (const Pedal&);
    void updateTailLength();
    int indexOf (NodeID) const;
    void notifyPedalsChanged();

    static void configureBuses (juce::AudioPluginInstance&);
    static juce::String encodeState (juce::AudioPluginInstance&);

    juce::SharedResourcePointer<PluginLibrary> library;
    juce::AudioProcessorGraph graph;
    juce::AudioProcessorGraph::Node::Ptr audioInNode, audioOutNode, midiInNode;

    // Guards pedals, the load queue, the pending restore and graph structure
    // against the host calling prepareToPlay / get/setStateInformation off the
    // message thread.
    mutable juce::CriticalSection chainLock;
    std::vector<Pedal> pedals;
    std::deque<PendingLoad> loadQueue;
    std::optional<PendingLoad> loadInFlight;
    std::unique_ptr<juce::XmlElement> pendingRestore; // set while a restore waits for the message thread
    int loadGeneration = 0;
    juce::uint32 nextMissingPedalId = 0x40000000; // ids for missing pedals, which have no graph node

    std::atomic<double> tailSeconds { 0.0 };
    int lastPedalLatencySum = 0;
    int timerTicks = 0;
    juce::ListenerList<Listener> listeners;

    // MIDI switching. Host MIDI arrives on the audio thread, device MIDI on
    // CoreMIDI threads; both are queued and handled on the message thread.
    MidiEventQueue hostMidi, deviceMidi;
    juce::SpinLock deviceMidiLock; // CoreMIDI may call back from more than one thread
    std::vector<std::unique_ptr<juce::MidiInput>> midiInputs;
    juce::MidiDeviceListConnection midiDeviceListConnection;
    juce::String midiInputSelection;
    NodeID midiLearnTarget {};
    std::map<NodeID, double> lastMidiToggleMs;
    std::optional<std::pair<MidiTrigger, double>> recentlyLearned;

    // Lets async callbacks detect that we've been deleted.
    std::shared_ptr<bool> aliveToken = std::make_shared<bool> (true);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedalboardProcessor)
};
