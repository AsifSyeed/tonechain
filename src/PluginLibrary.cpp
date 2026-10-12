#include "PluginLibrary.h"

namespace
{
constexpr auto knownPluginsKey = "knownPlugins";

juce::PropertiesFile::Options settingsOptions (juce::InterProcessLock& lock)
{
    juce::PropertiesFile::Options options;
    options.processLock = &lock;
    options.applicationName = "Tonechain";
    options.folderName = "Tonechain";
    options.filenameSuffix = "settings";
    options.osxLibrarySubFolder = "Application Support";
    return options;
}

// Builds before the rename were called "Pedalboard": carry their settings over.
void migrateSettingsFromPedalboard (const juce::File& settingsFile)
{
    const auto oldFolder = settingsFile.getParentDirectory().getSiblingFile ("Pedalboard");
    const auto oldFile = oldFolder.getChildFile ("Pedalboard.settings");

    if (settingsFile.existsAsFile() || ! oldFile.existsAsFile())
        return;

    settingsFile.getParentDirectory().createDirectory();
    oldFile.copyFileTo (settingsFile);
}
} // namespace

PluginLibrary::PluginLibrary()
{
    migrateSettingsFromPedalboard (settingsOptions (settingsLock).getDefaultFile());
    settings = std::make_unique<juce::PropertiesFile> (settingsOptions (settingsLock));

    juce::addDefaultFormatsToManager (formatManager);

    if (auto xml = settings->getXmlValue (knownPluginsKey))
        knownPlugins.recreateFromXml (*xml);

    lastSeenModification = settings->getFile().getLastModificationTime();
    knownPlugins.addChangeListener (this);
}

PluginLibrary::~PluginLibrary()
{
    knownPlugins.removeChangeListener (this);
    settings->saveIfNeeded();
}

juce::File PluginLibrary::getDeadMansPedalFile() const
{
    return settings->getFile().getSiblingFile ("RecentlyCrashedPluginsList");
}

bool PluginLibrary::isSelf (const juce::PluginDescription& d)
{
    // Match the AU codes too, so lists scanned before the rename (when this
    // plug-in was called "Pedalboard") never offer it as a pedal.
    return (d.name == JucePlugin_Name && d.manufacturerName == JucePlugin_Manufacturer)
           || d.fileOrIdentifier.containsIgnoreCase ("Pdbd,SyAu");
}

juce::Array<juce::PluginDescription> PluginLibrary::getEffects() const
{
    juce::Array<juce::PluginDescription> effects;

    for (const auto& d : knownPlugins.getTypes())
        if (! d.isInstrument && ! isSelf (d))
            effects.add (d);

    return effects;
}

void PluginLibrary::reloadIfChangedOnDisk()
{
    const auto modified = settings->getFile().getLastModificationTime();

    if (modified <= lastSeenModification)
        return;

    lastSeenModification = modified;
    settings->reload();

    if (auto xml = settings->getXmlValue (knownPluginsKey))
        knownPlugins.recreateFromXml (*xml);
}

void PluginLibrary::changeListenerCallback (juce::ChangeBroadcaster*)
{
    auto xml = knownPlugins.createXml();

    if (xml == nullptr)
        return;

    // Skip the echo of a reload: the file already holds exactly this list.
    if (settings->getValue (knownPluginsKey) == xml->toString (juce::XmlElement::TextFormat().singleLine().withoutHeader()))
        return;

    settings->setValue (knownPluginsKey, xml.get());
    settings->saveIfNeeded();
    lastSeenModification = settings->getFile().getLastModificationTime();
}
