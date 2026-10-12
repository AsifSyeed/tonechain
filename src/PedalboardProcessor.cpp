#include "PedalboardProcessor.h"
#include "ui/PedalboardEditor.h"

namespace
{
using UpdateKind = juce::AudioProcessorGraph::UpdateKind;
using IOProcessor = juce::AudioProcessorGraph::AudioGraphIOProcessor;

constexpr auto stateTag = "PEDALBOARD"; // kept from before the rename so saved projects load
constexpr auto pedalTag = "PEDAL";
constexpr auto stateAttr = "state";
constexpr auto bypassedAttr = "bypassed";
constexpr auto midiInputAttr = "midiInput";
constexpr double maxTailSeconds = 30.0;

constexpr int timerHz = 50;            // MIDI switching latency is at most one tick
constexpr int latencyCheckTicks = 12;  // ~4 times a second
constexpr double toggleDebounceMs = 40.0;
constexpr double learnSettleMs = 400.0;

bool isSwitchMessage (juce::uint8 status)
{
    const auto kind = status & 0xf0;
    return kind == 0xb0 || kind == 0x90 || kind == 0x80;
}
} // namespace

PedalboardProcessor::PedalboardProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    graph.setPlayConfigDetails (getMainBusNumInputChannels(), getMainBusNumOutputChannels(), 44100.0, 512);
    midiInNode = graph.addNode (std::make_unique<IOProcessor> (IOProcessor::midiInputNode), std::nullopt, UpdateKind::none);
    recreateIONodes();
    rebuildConnections();

    // In a DAW, listen to every MIDI device so footswitches work without any
    // routing. The standalone app already feeds us the MIDI input chosen in
    // its settings, so don't double up there.
    midiInputSelection = wrapperType == wrapperType_Standalone ? midiInputNone : midiInputAll;
    openMidiInputs();
    midiDeviceListConnection = juce::MidiDeviceListConnection::make ([this] { openMidiInputs(); });

    startTimerHz (timerHz);
}

PedalboardProcessor::~PedalboardProcessor()
{
    stopTimer();
    midiDeviceListConnection = {};
    midiInputs.clear(); // stop MIDI callbacks before anything else goes away
    aliveToken.reset();
    graph.clear();
}

//==============================================================================
int PedalboardProcessor::indexOf (NodeID nodeId) const
{
    for (size_t i = 0; i < pedals.size(); ++i)
        if (pedals[i].nodeId == nodeId)
            return (int) i;

    return -1;
}

void PedalboardProcessor::notifyPedalsChanged()
{
    listeners.call ([] (Listener& l) { l.pedalsChanged(); });
}

void PedalboardProcessor::addPedal (const juce::PluginDescription& description)
{
    JUCE_ASSERT_MESSAGE_THREAD

    {
        const juce::ScopedLock sl (chainLock);
        PendingLoad job;
        job.description = description;
        job.addedByUser = true;
        loadQueue.push_back (job);
    }

    loadNextInQueue();
    notifyPedalsChanged();
}

void PedalboardProcessor::removePedal (NodeID nodeId)
{
    JUCE_ASSERT_MESSAGE_THREAD

    const auto index = indexOf (nodeId);

    if (index < 0)
        return;

    listeners.call ([nodeId] (Listener& l) { l.pedalWillBeRemoved (nodeId); });

    {
        const juce::ScopedLock sl (chainLock);

        if (! pedals[(size_t) index].missing)
            graph.removeNode (nodeId, UpdateKind::none);

        pedals.erase (pedals.begin() + index);
        rebuildConnections();
    }

    lastMidiToggleMs.erase (nodeId);

    if (midiLearnTarget == nodeId)
        midiLearnTarget = {};

    updateTailLength();
    notifyPedalsChanged();
}

void PedalboardProcessor::movePedal (int fromIndex, int toIndex)
{
    JUCE_ASSERT_MESSAGE_THREAD

    const auto size = (int) pedals.size();

    if (! juce::isPositiveAndBelow (fromIndex, size) || fromIndex == toIndex)
        return;

    toIndex = juce::jlimit (0, size - 1, toIndex);

    {
        const juce::ScopedLock sl (chainLock);
        auto pedal = pedals[(size_t) fromIndex];
        pedals.erase (pedals.begin() + fromIndex);
        pedals.insert (pedals.begin() + toIndex, pedal);
        rebuildConnections();
    }

    notifyPedalsChanged();
}

void PedalboardProcessor::setPedalBypassed (NodeID nodeId, bool shouldBeBypassed)
{
    JUCE_ASSERT_MESSAGE_THREAD

    const auto index = indexOf (nodeId);

    if (index < 0 || pedals[(size_t) index].bypassed == shouldBeBypassed)
        return;

    {
        const juce::ScopedLock sl (chainLock);
        auto& pedal = pedals[(size_t) index];
        pedal.bypassed = shouldBeBypassed;
        applyBypassToNode (pedal);
        rebuildConnections();
    }

    updateTailLength();
    notifyPedalsChanged();
}

void PedalboardProcessor::clearPedals()
{
    JUCE_ASSERT_MESSAGE_THREAD

    for (const auto& p : pedals)
    {
        const auto nodeId = p.nodeId;
        listeners.call ([nodeId] (Listener& l) { l.pedalWillBeRemoved (nodeId); });
    }

    {
        const juce::ScopedLock sl (chainLock);

        for (const auto& p : pedals)
            if (! p.missing)
                graph.removeNode (p.nodeId, UpdateKind::none);

        pedals.clear();
        loadQueue.clear();
        loadInFlight.reset();
        ++loadGeneration; // whatever is still being instantiated gets discarded
        rebuildConnections();
    }

    midiLearnTarget = {};
    lastMidiToggleMs.clear();
    updateTailLength();
    notifyPedalsChanged();
}

std::vector<PedalboardProcessor::Pedal> PedalboardProcessor::getPedals() const
{
    const juce::ScopedLock sl (chainLock);
    return pedals;
}

int PedalboardProcessor::getNumPendingLoads() const
{
    const juce::ScopedLock sl (chainLock);
    return (int) loadQueue.size() + (loadInFlight.has_value() ? 1 : 0);
}

juce::AudioPluginInstance* PedalboardProcessor::getPedalInstance (NodeID nodeId) const
{
    if (auto* node = graph.getNodeForId (nodeId))
        return dynamic_cast<juce::AudioPluginInstance*> (node->getProcessor());

    return nullptr;
}

//==============================================================================
void PedalboardProcessor::loadNextInQueue()
{
    PendingLoad job;
    int generation = 0;

    {
        const juce::ScopedLock sl (chainLock);

        if (loadInFlight.has_value() || loadQueue.empty())
            return;

        loadInFlight = loadQueue.front();
        loadQueue.pop_front();
        job = *loadInFlight;
        generation = loadGeneration;
    }

    const auto sampleRate = getSampleRate() > 0.0 ? getSampleRate() : 44100.0;
    const auto blockSize = getBlockSize() > 0 ? getBlockSize() : 512;
    std::weak_ptr<bool> alive = aliveToken;

    library->formatManager.createPluginInstanceAsync (
        job.description, sampleRate, blockSize,
        [this, alive, generation] (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
        {
            if (alive.expired())
                return;

            std::optional<PendingLoad> finished;

            {
                const juce::ScopedLock sl (chainLock);

                // A clear or restore happened since this load started; its
                // replacement chain is already loading.
                if (generation != loadGeneration)
                    return;

                finished = std::move (loadInFlight);
                loadInFlight.reset();
            }

            if (finished.has_value())
            {
                if (instance != nullptr)
                {
                    installPedal (std::move (instance), *finished);
                }
                else
                {
                    if (! finished->addedByUser)
                        installMissingPedal (*finished, error);

                    const auto name = finished->description.name;
                    listeners.call ([&] (Listener& l) { l.loadFailed (name, error); });
                }
            }

            loadNextInQueue();
            notifyPedalsChanged();
        });
}

void PedalboardProcessor::installPedal (std::unique_ptr<juce::AudioPluginInstance> instance, const PendingLoad& job)
{
    configureBuses (*instance);

    if (job.base64State.isNotEmpty())
    {
        juce::MemoryBlock state;

        if (state.fromBase64Encoding (job.base64State))
            instance->setStateInformation (state.getData(), (int) state.getSize());
    }

    NodeID nodeId;

    {
        const juce::ScopedLock sl (chainLock);

        auto node = graph.addNode (std::move (instance), std::nullopt, UpdateKind::none);

        if (node == nullptr)
        {
            if (! job.addedByUser)
                installMissingPedal (job, "Couldn't add the plug-in to the board");

            return;
        }

        Pedal pedal;
        pedal.nodeId = node->nodeID;
        pedal.description = job.description;
        pedal.bypassed = job.bypassed;
        pedal.midi = job.midi;
        pedals.push_back (pedal);

        applyBypassToNode (pedal);
        rebuildConnections();
        nodeId = pedal.nodeId;
    }

    updateTailLength();
    listeners.call ([nodeId, byUser = job.addedByUser] (Listener& l) { l.pedalAdded (nodeId, byUser); });
}

void PedalboardProcessor::installMissingPedal (const PendingLoad& job, const juce::String& reason)
{
    Pedal pedal;
    pedal.nodeId = NodeID { nextMissingPedalId++ };
    pedal.description = job.description;
    pedal.bypassed = job.bypassed;
    pedal.midi = job.midi;
    pedal.missing = true;
    pedal.missingReason = reason.isNotEmpty() ? reason : juce::String ("The plug-in isn't installed on this Mac");
    pedal.missingState = job.base64State;

    const juce::ScopedLock sl (chainLock);
    pedals.push_back (pedal);
}

void PedalboardProcessor::configureBuses (juce::AudioPluginInstance& instance)
{
    using CS = juce::AudioChannelSet;
    const std::pair<CS, CS> attempts[] { { CS::stereo(), CS::stereo() },
                                         { CS::mono(), CS::stereo() },
                                         { CS::mono(), CS::mono() } };

    for (const auto& [in, out] : attempts)
    {
        auto layout = instance.getBusesLayout();

        if (layout.outputBuses.isEmpty())
            return;

        layout.outputBuses.getReference (0) = out;

        if (! layout.inputBuses.isEmpty())
            layout.inputBuses.getReference (0) = in;

        if (instance.setBusesLayout (layout))
            return;
    }
}

void PedalboardProcessor::applyBypassToNode (const Pedal& pedal)
{
    if (pedal.missing)
        return;

    auto* node = graph.getNodeForId (pedal.nodeId);

    if (node == nullptr)
        return;

    // The pedal is also wired around, so this only saves CPU: AUs with their
    // own bypass switch into it, the rest get the graph's cheap pass-through.
    node->setBypassed (pedal.bypassed);

    if (auto* bypassParam = node->getProcessor()->getBypassParameter())
        bypassParam->setValueNotifyingHost (pedal.bypassed ? 1.0f : 0.0f);
}

//==============================================================================
void PedalboardProcessor::recreateIONodes()
{
    if (audioInNode != nullptr)
        graph.removeNode (audioInNode.get(), UpdateKind::none);

    if (audioOutNode != nullptr)
        graph.removeNode (audioOutNode.get(), UpdateKind::none);

    // IO nodes pick up the graph's channel count when they are added.
    audioInNode = graph.addNode (std::make_unique<IOProcessor> (IOProcessor::audioInputNode), std::nullopt, UpdateKind::none);
    audioOutNode = graph.addNode (std::make_unique<IOProcessor> (IOProcessor::audioOutputNode), std::nullopt, UpdateKind::none);
}

void PedalboardProcessor::connectChannels (NodeID source, int numSourceChannels, NodeID dest, int numDestChannels)
{
    if (numSourceChannels <= 0)
        return;

    // Mono -> stereo duplicates the signal; stereo -> mono takes the left side.
    for (int ch = 0; ch < numDestChannels; ++ch)
        graph.addConnection ({ { source, juce::jmin (ch, numSourceChannels - 1) }, { dest, ch } }, UpdateKind::none);
}

void PedalboardProcessor::rebuildConnections()
{
    const juce::ScopedLock sl (chainLock);

    for (const auto& c : graph.getConnections())
        graph.removeConnection (c, UpdateKind::none);

    auto source = audioInNode->nodeID;
    auto numSourceChannels = audioInNode->getProcessor()->getTotalNumOutputChannels();

    for (const auto& pedal : pedals)
    {
        if (pedal.missing)
            continue;

        auto* node = graph.getNodeForId (pedal.nodeId);

        if (node == nullptr)
            continue;

        auto* proc = node->getProcessor();

        // Pedals get the host's MIDI too, for their own MIDI mappings.
        if (proc->acceptsMidi())
            graph.addConnection ({ { midiInNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex },
                                   { pedal.nodeId, juce::AudioProcessorGraph::midiChannelIndex } },
                                 UpdateKind::none);

        const auto numOuts = proc->getMainBusNumOutputChannels();

        if (pedal.bypassed || numOuts == 0)
            continue;

        connectChannels (source, numSourceChannels, pedal.nodeId, proc->getMainBusNumInputChannels());
        source = pedal.nodeId;
        numSourceChannels = numOuts;
    }

    connectChannels (source, numSourceChannels, audioOutNode->nodeID, audioOutNode->getProcessor()->getTotalNumInputChannels());
    graph.rebuild();
}

void PedalboardProcessor::updateTailLength()
{
    double total = 0.0;

    {
        const juce::ScopedLock sl (chainLock);

        for (const auto& p : pedals)
            if (! p.bypassed)
                if (auto* instance = getPedalInstance (p.nodeId))
                    total += juce::jmax (0.0, instance->getTailLengthSeconds());
    }

    tailSeconds = juce::jmin (total, maxTailSeconds);
}

void PedalboardProcessor::checkLatency()
{
    int pedalLatencySum = 0;

    {
        const juce::ScopedLock sl (chainLock);

        for (const auto& p : pedals)
            if (! p.bypassed)
                if (auto* instance = getPedalInstance (p.nodeId))
                    pedalLatencySum += instance->getLatencySamples();

        // The graph only recalculates latency when it rebuilds, so rebuild
        // when a pedal changes its own latency (e.g. a lookahead mode).
        if (pedalLatencySum != lastPedalLatencySum)
        {
            lastPedalLatencySum = pedalLatencySum;
            graph.rebuild();
        }
    }

    if (graph.getLatencySamples() != getLatencySamples())
        setLatencySamples (graph.getLatencySamples());
}

//==============================================================================
void PedalboardProcessor::startMidiLearn (NodeID nodeId)
{
    if (indexOf (nodeId) < 0)
        return;

    midiLearnTarget = nodeId;
    notifyPedalsChanged();
}

void PedalboardProcessor::cancelMidiLearn()
{
    if (midiLearnTarget == NodeID{})
        return;

    midiLearnTarget = {};
    notifyPedalsChanged();
}

void PedalboardProcessor::setPedalMidiTrigger (NodeID nodeId, const MidiTrigger& trigger)
{
    const auto index = indexOf (nodeId);

    if (index < 0)
        return;

    {
        const juce::ScopedLock sl (chainLock);
        pedals[(size_t) index].midi = trigger;
    }

    notifyPedalsChanged();
}

void PedalboardProcessor::setMidiInputSelection (const juce::String& selection)
{
    midiInputSelection = selection;
    openMidiInputs();
    notifyPedalsChanged();
}

void PedalboardProcessor::openMidiInputs()
{
    midiInputs.clear();

    if (midiInputSelection == midiInputNone)
        return;

    for (const auto& device : juce::MidiInput::getAvailableDevices())
    {
        if (midiInputSelection != midiInputAll && device.identifier != midiInputSelection)
            continue;

        if (auto input = juce::MidiInput::openDevice (device.identifier, this))
        {
            input->start();
            midiInputs.push_back (std::move (input));
        }
    }
}

void PedalboardProcessor::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message)
{
    // CoreMIDI thread.
    const auto* data = message.getRawData();

    if (message.getRawDataSize() < 3 || ! isSwitchMessage (data[0]))
        return;

    const juce::SpinLock::ScopedLockType sl (deviceMidiLock);
    deviceMidi.push ({ data[0], data[1], data[2] });
}

void PedalboardProcessor::timerCallback()
{
    hostMidi.drain ([this] (MidiEvent e) { handleMidiEvent (e); });
    deviceMidi.drain ([this] (MidiEvent e) { handleMidiEvent (e); });

    if (++timerTicks % latencyCheckTicks == 0)
        checkLatency();
}

void PedalboardProcessor::handleMidiEvent (MidiEvent e)
{
    const auto kind = e.status & 0xf0;
    const auto channel = (e.status & 0x0f) + 1;
    const auto number = (int) e.data1;
    const auto type = kind == 0xb0 ? MidiTrigger::Type::controller : MidiTrigger::Type::note;
    const auto pressed = kind == 0xb0 ? e.data2 >= 64 : (kind == 0x90 && e.data2 > 0);
    const auto now = juce::Time::getMillisecondCounterHiRes();

    if (midiLearnTarget != NodeID{})
    {
        if (type == MidiTrigger::Type::note && ! pressed)
            return; // learn notes from the note-on, not a stray note-off

        const auto index = indexOf (midiLearnTarget);

        if (index < 0)
        {
            cancelMidiLearn();
            return;
        }

        auto& pedal = pedals[(size_t) index];
        MidiTrigger trigger { type, channel, number, pedal.midi.isAssigned() ? pedal.midi.mode : MidiTrigger::Mode::toggle };

        {
            const juce::ScopedLock sl (chainLock);
            pedal.midi = trigger;
        }

        const auto nodeId = midiLearnTarget;
        midiLearnTarget = {};
        recentlyLearned = std::make_pair (trigger, now);

        listeners.call ([&] (Listener& l) { l.midiLearned (nodeId, trigger); });
        notifyPedalsChanged();
        return;
    }

    // Ignore the rest of the press that was just used for learning (e.g. the
    // release of a momentary switch), so learning never also switches the pedal.
    if (recentlyLearned.has_value())
    {
        if (recentlyLearned->first.matches (type, channel, number) && now - recentlyLearned->second < learnSettleMs)
            return;

        recentlyLearned.reset();
    }

    std::vector<std::pair<NodeID, bool>> changes;

    for (const auto& pedal : pedals)
    {
        if (! pedal.midi.matches (type, channel, number))
            continue;

        if (pedal.midi.mode == MidiTrigger::Mode::follow)
        {
            changes.emplace_back (pedal.nodeId, ! pressed);
        }
        else if (pressed)
        {
            // Debounced, so a press arriving from both the host and the device
            // at once only toggles once.
            auto& last = lastMidiToggleMs[pedal.nodeId];

            if (now - last >= toggleDebounceMs)
            {
                last = now;
                changes.emplace_back (pedal.nodeId, ! pedal.bypassed);
            }
        }
    }

    for (const auto& [nodeId, bypassed] : changes)
        setPedalBypassed (nodeId, bypassed);
}

//==============================================================================
void PedalboardProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const auto numIns = getMainBusNumInputChannels();
    const auto numOuts = getMainBusNumOutputChannels();

    {
        const juce::ScopedLock sl (chainLock);
        const auto channelsChanged = numIns != graph.getTotalNumInputChannels()
                                     || numOuts != graph.getTotalNumOutputChannels();

        graph.setPlayConfigDetails (numIns, numOuts, sampleRate, samplesPerBlock);

        if (channelsChanged)
        {
            recreateIONodes();
            rebuildConnections();
        }
    }

    graph.prepareToPlay (sampleRate, samplesPerBlock);
}

void PedalboardProcessor::releaseResources()
{
    graph.releaseResources();
}

bool PedalboardProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    const auto isMonoOrStereo = [] (const juce::AudioChannelSet& s)
    {
        return s == juce::AudioChannelSet::mono() || s == juce::AudioChannelSet::stereo();
    };

    return isMonoOrStereo (out) && isMonoOrStereo (in) && in.size() <= out.size();
}

void PedalboardProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    for (const auto metadata : midi)
        if (metadata.numBytes >= 3 && isSwitchMessage (metadata.data[0]))
            hostMidi.push ({ metadata.data[0], metadata.data[1], metadata.data[2] });

    for (auto ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    graph.setPlayHead (getPlayHead());
    graph.processBlock (buffer, midi);
}

void PedalboardProcessor::reset()
{
    graph.reset();
}

void PedalboardProcessor::setNonRealtime (bool isNonRealtime) noexcept
{
    AudioProcessor::setNonRealtime (isNonRealtime);
    graph.setNonRealtime (isNonRealtime);
}

juce::AudioProcessorEditor* PedalboardProcessor::createEditor()
{
    return new PedalboardEditor (*this);
}

//==============================================================================
juce::String PedalboardProcessor::encodeState (juce::AudioPluginInstance& instance)
{
    juce::MemoryBlock state;
    instance.getStateInformation (state);
    return state.toBase64Encoding();
}

void PedalboardProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    std::vector<std::pair<Pedal, juce::AudioProcessorGraph::Node::Ptr>> loaded;
    std::vector<PendingLoad> loading;
    juce::String midiInput;

    {
        const juce::ScopedLock sl (chainLock);

        // A restore that hasn't reached the message thread yet is the real state.
        if (pendingRestore != nullptr)
        {
            copyXmlToBinary (*pendingRestore, destData);
            return;
        }

        for (const auto& p : pedals)
            loaded.emplace_back (p, p.missing ? nullptr : graph.getNodeForId (p.nodeId));

        // Pedals still loading (e.g. right after a session restore) must survive a save.
        if (loadInFlight.has_value())
            loading.push_back (*loadInFlight);

        loading.insert (loading.end(), loadQueue.begin(), loadQueue.end());
        midiInput = midiInputSelection;
    }

    // Ask the plugins for their state without holding the lock: some AUs block
    // on the main thread here, which may itself be waiting for the lock.
    juce::XmlElement root (stateTag);
    root.setAttribute ("version", 2);
    root.setAttribute (midiInputAttr, midiInput);

    const auto addPedalXml = [&root] (const juce::PluginDescription& d, const juce::String& state, bool bypassed, const MidiTrigger& midi)
    {
        auto* xml = root.createNewChildElement (pedalTag);
        xml->addChildElement (d.createXml().release());
        xml->setAttribute (stateAttr, state);
        xml->setAttribute (bypassedAttr, bypassed);
        midi.writeTo (*xml);
    };

    for (const auto& [pedal, node] : loaded)
    {
        juce::String state = pedal.missingState;

        if (node != nullptr)
            if (auto* instance = dynamic_cast<juce::AudioPluginInstance*> (node->getProcessor()))
                state = encodeState (*instance);

        addPedalXml (pedal.description, state, pedal.bypassed, pedal.midi);
    }

    for (const auto& job : loading)
        addPedalXml (job.description, job.base64State, job.bypassed, job.midi);

    copyXmlToBinary (root, destData);
}

void PedalboardProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (stateTag))
        return;

    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        {
            const juce::ScopedLock sl (chainLock);
            pendingRestore.reset();
        }

        restoreFromXml (*xml);
        return;
    }

    {
        const juce::ScopedLock sl (chainLock);
        pendingRestore = std::move (xml);
    }

    std::weak_ptr<bool> alive = aliveToken;
    juce::MessageManager::callAsync ([this, alive]
    {
        if (alive.expired())
            return;

        std::unique_ptr<juce::XmlElement> toRestore;

        {
            const juce::ScopedLock sl (chainLock);
            toRestore = std::move (pendingRestore);
        }

        if (toRestore != nullptr)
            restoreFromXml (*toRestore);
    });
}

void PedalboardProcessor::restoreFromXml (const juce::XmlElement& root)
{
    clearPedals();

    if (root.hasAttribute (midiInputAttr))
    {
        midiInputSelection = root.getStringAttribute (midiInputAttr);
        openMidiInputs();
    }

    {
        const juce::ScopedLock sl (chainLock);

        for (auto* pedalXml : root.getChildWithTagNameIterator (pedalTag))
        {
            PendingLoad job;

            if (auto* descXml = pedalXml->getChildByName ("PLUGIN"); descXml == nullptr || ! job.description.loadFromXml (*descXml))
                continue;

            job.base64State = pedalXml->getStringAttribute (stateAttr);
            job.bypassed = pedalXml->getBoolAttribute (bypassedAttr);
            job.midi = MidiTrigger::readFrom (*pedalXml);
            loadQueue.push_back (job);
        }
    }

    loadNextInQueue();
    notifyPedalsChanged();
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PedalboardProcessor();
}
