#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

// The list of Audio Units the user has scanned, shared by every Tonechain
// instance in the process and persisted to
// ~/Library/Application Support/Tonechain/Tonechain.settings.
// Access it through juce::SharedResourcePointer<PluginLibrary>.
class PluginLibrary : private juce::ChangeListener
{
public:
    PluginLibrary();
    ~PluginLibrary() override;

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;

    juce::PropertiesFile& getSettings() { return *settings; }
    juce::File getDeadMansPedalFile() const;

    // Scanned effects (no instruments, never ourselves), ready for a menu.
    juce::Array<juce::PluginDescription> getEffects() const;

    static bool isSelf (const juce::PluginDescription&);

    // Picks up scans saved by Tonechain instances in other processes (Logic
    // can host plug-ins out of process). Call before showing the list.
    void reloadIfChangedOnDisk();

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    juce::InterProcessLock settingsLock { "TonechainSettings" }; // must outlive settings
    std::unique_ptr<juce::PropertiesFile> settings;
    juce::Time lastSeenModification;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginLibrary)
};
