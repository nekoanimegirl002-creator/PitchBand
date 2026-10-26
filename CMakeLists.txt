cmake_minimum_required(VERSION 3.22)
project(PitchBend VERSION 0.1.0)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

option(COPY_AFTER_BUILD "Copy the plugin to the system VST3 folder after build" ON)

juce_add_plugin(PitchBend
    COMPANY_NAME "MyStudio"
    PLUGIN_MANUFACTURER_CODE Mstd
    PLUGIN_CODE Pbnd
    FORMATS VST3 Standalone
    PRODUCT_NAME "Pitch Bend"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD ${COPY_AFTER_BUILD})

target_sources(PitchBend PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(PitchBend PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(PitchBend
    PRIVATE juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags
           juce::juce_recommended_warning_flags)
