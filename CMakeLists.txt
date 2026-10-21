# AutoWarp - automatic warp / quantize plugin
# Created by DogSide Productions & Bluethedoggo
cmake_minimum_required(VERSION 3.22)

# Link the MSVC runtime statically so the plugin runs on any Windows PC
# without needing the "Visual C++ Redistributable" installed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(AutoWarp VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- JUCE ---------------------------------------------------------------------
# Use a local JUCE checkout with  -DAUTOWARP_JUCE_PATH=C:/path/to/JUCE
# otherwise it is downloaded automatically.
set(AUTOWARP_JUCE_PATH "" CACHE PATH "Optional path to a local JUCE checkout")

if(AUTOWARP_JUCE_PATH)
    add_subdirectory(${AUTOWARP_JUCE_PATH} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.9
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ---- Plugin -------------------------------------------------------------------
juce_add_plugin(AutoWarp
    COMPANY_NAME                "DogSide Productions"
    COMPANY_WEBSITE             ""
    PLUGIN_MANUFACTURER_CODE    Dgsd
    PLUGIN_CODE                 Awrp
    PRODUCT_NAME                "AutoWarp"
    DESCRIPTION                 "Automatic warp and quantize by DogSide Productions and Bluethedoggo"
    BUNDLE_ID                   com.dogsideproductions.autowarp
    FORMATS                     VST3 Standalone
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    VST3_CATEGORIES             Instrument Sampler
    COPY_PLUGIN_AFTER_BUILD     FALSE)

target_sources(AutoWarp PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/WarpView.cpp
    Source/WarpView.h
    Source/WarpEngine.cpp
    Source/WarpEngine.h)

target_compile_definitions(AutoWarp PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(AutoWarp
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
