cmake_minimum_required(VERSION 3.15)
project(WaveSlave VERSION 1.0.0)

# Fetch JUCE so the user doesn't need to manually install it
include(FetchContent)
FetchContent_Declare(
    juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        7.0.9
)
FetchContent_MakeAvailable(juce)

juce_set_vst2_sdk_path("${CMAKE_CURRENT_SOURCE_DIR}/vst2sdk/vstsdk2.4")

# Create a JUCE plugin
juce_add_plugin(WaveSlave
    COMPANY_NAME "WaveSlaveAudio"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE WvSl
    PLUGIN_CODE WvSl
    FORMATS VST VST3 Standalone
)

juce_generate_juce_header(WaveSlave)

target_sources(WaveSlave PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/Visualizer.cpp
    Source/Visualizer.h
    Source/SynthVoice.cpp
    Source/SynthVoice.h
)

target_compile_definitions(WaveSlave
    PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(WaveSlave
    PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
    juce::juce_audio_processors
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
