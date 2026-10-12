cmake_minimum_required(VERSION 3.22)

project(Tonechain VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build for Apple Silicon and Intel unless told otherwise.
if(NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

# JUCE resolution order:
# 1) JUCE_SOURCE_DIR cache/env var
# 2) sibling checkout at ../JUCE
# 3) download the pinned release
set(TONECHAIN_JUCE_VERSION 8.0.12)
set(JUCE_SOURCE_DIR "$ENV{JUCE_SOURCE_DIR}" CACHE PATH "Path to JUCE root")

if(JUCE_SOURCE_DIR AND EXISTS "${JUCE_SOURCE_DIR}/CMakeLists.txt")
    add_subdirectory("${JUCE_SOURCE_DIR}" JUCE)
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../JUCE/CMakeLists.txt")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../JUCE" JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG ${TONECHAIN_JUCE_VERSION}
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(Tonechain
    VERSION ${PROJECT_VERSION}
    COMPANY_NAME "Asif Syeed"
    BUNDLE_ID "com.asifsyeed.tonechain"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    # Stays a plain effect (aufx): changing the AU type breaks existing Logic
    # projects. Footswitches reach us by reading MIDI devices directly; the
    # MIDI input is used by the standalone app and hosts that send MIDI to effects.
    # (JUCE would default to aumf because of NEEDS_MIDI_INPUT, so set it explicitly.)
    AU_MAIN_TYPE kAudioUnitType_Effect
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    MICROPHONE_PERMISSION_ENABLED TRUE
    # Never change these two codes: hosts identify the plug-in by them, and
    # projects saved with earlier builds (named "Pedalboard") depend on them.
    PLUGIN_MANUFACTURER_CODE SyAu
    PLUGIN_CODE Pdbd
    FORMATS AU Standalone
    PRODUCT_NAME "Tonechain"
)

target_sources(Tonechain
    PRIVATE
        src/PluginLibrary.cpp
        src/PedalboardProcessor.cpp
        src/ui/PedalComponent.cpp
        src/ui/PedalboardEditor.cpp
)

target_include_directories(Tonechain PRIVATE src)

target_compile_definitions(Tonechain
    PUBLIC
        JUCE_PLUGINHOST_AU=1
        JUCE_PLUGINHOST_VST3=0
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(Tonechain
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
