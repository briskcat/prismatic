cmake_minimum_required(VERSION 3.22)

# The plug-in name shown in DAWs and on the UI
set(PLUGIN_NAME "Prism FX")

project(PrismFX VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
# Builds for this Mac's chip only. For a universal (Intel + Apple Silicon)
# release build, configure with -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64".

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.3
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

# Copy the built AU / VST3 into ~/Library/Audio/Plug-Ins so Logic and Ableton find them.
option(PRISM_INSTALL_PLUGINS "Copy plug-ins to the user plug-in folders after building" ON)

juce_add_plugin(PrismFX
    PRODUCT_NAME             "${PLUGIN_NAME}"
    COMPANY_NAME             "Prismatic"
    BUNDLE_ID                "com.prismatic.prismfx"
    PLUGIN_MANUFACTURER_CODE Prsm
    PLUGIN_CODE              Pfx1
    FORMATS                  AU VST3 Standalone
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         FALSE
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    AU_MAIN_TYPE             kAudioUnitType_Effect
    VST3_CATEGORIES          Fx Delay Reverb Distortion
    COPY_PLUGIN_AFTER_BUILD  ${PRISM_INSTALL_PLUGINS})

juce_add_binary_data(PrismAssets SOURCES
    Assets/fonts/InstrumentSerif-Regular.ttf
    Assets/fonts/InstrumentSerif-Italic.ttf
    Assets/fonts/DMMono-Regular.ttf
    Assets/fonts/DMMono-Medium.ttf)

target_sources(PrismFX PRIVATE
    Source/ui/Theme.cpp
    Source/Parameters.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(PrismFX PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(PrismFX
    PRIVATE
        juce::juce_audio_utils
        PrismAssets
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# Link-time optimisation makes release binaries a bit faster but linking very
# slow and CPU-hungry. Off by default; turn on for release builds.
option(PRISM_LTO "Enable link-time optimisation" OFF)
if(PRISM_LTO)
    target_link_libraries(PrismFX PUBLIC juce::juce_recommended_lto_flags)
endif()

# Dev tool that renders the editor to a PNG: cmake -DPRISM_SNAPSHOT=ON
option(PRISM_SNAPSHOT "Build the UI snapshot tool" OFF)
if(PRISM_SNAPSHOT)
    juce_add_console_app(PrismSnapshot PRODUCT_NAME "PrismSnapshot")
    target_sources(PrismSnapshot PRIVATE
        Tools/Snapshot.cpp
        Source/Parameters.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/ui/Theme.cpp)
    target_compile_definitions(PrismSnapshot PRIVATE
        JucePlugin_Name="${PLUGIN_NAME}" JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(PrismSnapshot PRIVATE juce::juce_audio_utils PrismAssets juce::juce_recommended_config_flags)

    juce_add_console_app(PrismStateTest PRODUCT_NAME "PrismStateTest")
    target_sources(PrismStateTest PRIVATE
        Tools/StateTest.cpp
        Source/Parameters.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/ui/Theme.cpp)
    target_compile_definitions(PrismStateTest PRIVATE
        JucePlugin_Name="${PLUGIN_NAME}" JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(PrismStateTest PRIVATE juce::juce_audio_utils PrismAssets juce::juce_recommended_config_flags)
endif()
