cmake_minimum_required(VERSION 3.22)

# Link the Windows runtime into the plugin, so you do not need to install
# the "Visual C++ Redistributable" on your PC.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(GigGoatPlayer VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# JUCE: the plugin framework
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.9
    GIT_SHALLOW    ON)

# libgig: reads .gig files (Surge's small CMake-friendly copy of libgig 4.3.0)
FetchContent_Declare(libgig
    GIT_REPOSITORY https://github.com/surge-synthesizer/libgig-modified.git
    GIT_TAG        c32f2271998c842b72cac5dfab15233f5cea6555
    EXCLUDE_FROM_ALL)

FetchContent_MakeAvailable(JUCE libgig)
set_target_properties(libgig PROPERTIES POSITION_INDEPENDENT_CODE ON)

juce_add_plugin(GigGoatPlayer
    COMPANY_NAME                "Deepspace"
    PLUGIN_MANUFACTURER_CODE    Dpsp
    PLUGIN_CODE                 GgPl
    PRODUCT_NAME                "Gig Goat Player"
    FORMATS                     VST3 Standalone
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    VST3_CATEGORIES             Instrument Sampler
    COPY_PLUGIN_AFTER_BUILD     FALSE)

target_sources(GigGoatPlayer PRIVATE
    Source/GigEngine.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_include_directories(GigGoatPlayer PRIVATE Source)

target_compile_definitions(GigGoatPlayer PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(GigGoatPlayer
    PRIVATE
        juce::juce_audio_utils
        libgig
    PUBLIC
        juce::juce_recommended_config_flags)
