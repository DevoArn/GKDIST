# GKDIST - 24-band multiband distortion plug-in
# Copyright (C) 2026 Roman Gutekunst (GK)
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.20)

project(GKDIST VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static MSVC runtime: no dependency on an installed VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_subdirectory(external/JUCE)

# PFFFT (BSD-style licence): FFT for the spectrum analyser
add_library(pffft STATIC external/pffft/src/pffft.c external/pffft/src/pffft_common.c)
target_include_directories(pffft PUBLIC external/pffft/include/pffft PRIVATE external/pffft/src)
target_compile_definitions(pffft PUBLIC PFFFT_STATIC_DEFINE PRIVATE _USE_MATH_DEFINES)
if(MSVC)
    target_compile_options(pffft PRIVATE /O2 /fp:fast /W0)
endif()

set(GKD_DSP_SOURCES
    Source/Params.cpp
    Source/dsp/FilterDesign.cpp
    Source/dsp/Oversampler.cpp
    Source/dsp/BandProcessor.cpp
    Source/dsp/DistEngine.cpp)

set(GKD_UI_SOURCES
    Source/ui/Theme.cpp
    Source/ui/Spectrum.cpp
    Source/ui/DistDisplay.cpp
    Source/ui/Controls.cpp
    Source/ui/BandPanel.cpp
    Source/ui/ChainStrip.cpp
    Source/ui/MixerPanel.cpp
    Source/ui/ModPanel.cpp
    Source/ui/WaveformView.cpp
    Source/ui/GlobalPanel.cpp
    Source/ui/TopBar.cpp)

set(GKD_APP_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Presets.cpp)

option(GKD_BUILD_PLUGIN "Build the plug-in (disable to build only the DSP tests)" ON)

if(GKD_BUILD_PLUGIN)
    juce_add_plugin(GKDIST
        COMPANY_NAME "GK"
        COMPANY_WEBSITE ""
        PLUGIN_MANUFACTURER_CODE MdAu
        PLUGIN_CODE GkDs
        FORMATS VST3 Standalone
        PRODUCT_NAME "GKDIST"
        BUNDLE_ID "com.gk.gkdist"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        VST3_CATEGORIES Fx Distortion
        ICON_BIG Resources/icon.png
        VERSION ${PROJECT_VERSION}
        COPY_PLUGIN_AFTER_BUILD FALSE)

    target_sources(GKDIST PRIVATE ${GKD_APP_SOURCES} ${GKD_DSP_SOURCES} ${GKD_UI_SOURCES})
    target_include_directories(GKDIST PRIVATE Source)
    target_compile_definitions(GKDIST PUBLIC
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0 JUCE_MODAL_LOOPS_PERMITTED=0)
    target_link_libraries(GKDIST
        PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_opengl pffft
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
    if(MSVC)
        target_compile_options(GKDIST PRIVATE /fp:precise /bigobj)
    endif()

    # Host-side test: loads the built VST3 like a DAW.
    juce_add_console_app(GKDISTHostTest PRODUCT_NAME "GKDISTHostTest")
    target_sources(GKDISTHostTest PRIVATE Tests/HostTest.cpp)
    target_compile_definitions(GKDISTHostTest PRIVATE
        JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_DISPLAY_SPLASH_SCREEN=0)
    target_link_libraries(GKDISTHostTest
        PRIVATE juce::juce_audio_utils juce::juce_audio_processors
        PUBLIC juce::juce_recommended_config_flags)

    # In-process UI test: renders the editor, simulates interaction, hostile state.
    juce_add_console_app(GKDISTUITest PRODUCT_NAME "GKDISTUITest")
    target_sources(GKDISTUITest PRIVATE Tests/UITest.cpp ${GKD_APP_SOURCES} ${GKD_DSP_SOURCES} ${GKD_UI_SOURCES})
    target_include_directories(GKDISTUITest PRIVATE Source)
    target_compile_definitions(GKDISTUITest PRIVATE
        JucePlugin_Name="GKDIST" JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_DISPLAY_SPLASH_SCREEN=0)
    target_link_libraries(GKDISTUITest
        PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_opengl pffft
        PUBLIC juce::juce_recommended_config_flags)
endif()

# DSP test runner
juce_add_console_app(GKDISTTests PRODUCT_NAME "GKDISTTests")
target_sources(GKDISTTests PRIVATE Tests/Tests.cpp ${GKD_DSP_SOURCES})
target_include_directories(GKDISTTests PRIVATE Source)
target_compile_definitions(GKDISTTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1)
target_link_libraries(GKDISTTests
    PRIVATE juce::juce_audio_processors juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# Windows installer (build.bat installer): embeds build/installer/payload.zip
option(GKD_BUILD_INSTALLER "Build the Windows installer" OFF)
if(GKD_BUILD_INSTALLER)
    set(GKD_PAYLOAD "" CACHE FILEPATH "Installer payload (zip made by Tools/make_payload.py)")
    if(NOT EXISTS "${GKD_PAYLOAD}")
        message(FATAL_ERROR "Installer payload not found: '${GKD_PAYLOAD}'")
    endif()
    juce_add_binary_data(GKDISTPayload HEADER_NAME PayloadData.h NAMESPACE Payload SOURCES "${GKD_PAYLOAD}")

    # GKDISTSetup: the real installer (administrator rights).
    # GKDISTSetupTest: identical without the UAC requirement, for the automated test.
    foreach(setup_target GKDISTSetup GKDISTSetupTest)
        juce_add_gui_app(${setup_target}
            PRODUCT_NAME "GKDIST Setup"
            COMPANY_NAME "GK"
            VERSION ${PROJECT_VERSION}
            ICON_BIG Resources/icon.png)
        target_sources(${setup_target} PRIVATE Installer/SetupApp.cpp Installer/Win32.cpp)
        target_compile_definitions(${setup_target} PRIVATE
            GKD_VERSION="${PROJECT_VERSION}" JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
            JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0)
        target_link_libraries(${setup_target}
            PRIVATE GKDISTPayload juce::juce_gui_basics shell32 ole32 uuid advapi32
            PUBLIC juce::juce_recommended_config_flags)
        if(MSVC)
            # Load DLLs from System32 only (DLL planting protection).
            target_link_options(${setup_target} PRIVATE "/DEPENDENTLOADFLAG:0x800")
        endif()
    endforeach()
    if(MSVC)
        target_link_options(GKDISTSetup PRIVATE "/MANIFESTUAC:level='requireAdministrator'")
    endif()
endif()
