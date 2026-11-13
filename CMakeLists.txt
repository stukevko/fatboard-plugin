cmake_minimum_required(VERSION 3.22)
project(Fatboard VERSION 0.8.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# MSVC: compile the files of one target in parallel (VS only parallelises whole targets otherwise)
if(MSVC)
  add_compile_options(/MP)
endif()
# Link-time optimisation makes release builds a little faster at runtime but
# doubles link time on Windows. Off for test builds, on for tagged releases.
option(FATBOARD_LTO "Link-time optimisation" OFF)

# macOS: universal binaries (Intel + Apple Silicon) when building on a Mac
if(APPLE)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
endif()

# JUCE: use the local folder if present (git submodule / clone), else fetch it
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
  add_subdirectory(JUCE)
else()
  include(FetchContent)
  FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(JUCE)
endif()

# ---- Embedded assets (fonts) ------------------------------------------------
juce_add_binary_data(FatboardAssets
  NAMESPACE FatboardAssets
  SOURCES
    Assets/UnboundedBlack.ttf
    Assets/UnboundedBold.ttf
    Assets/InterMedium.ttf
    Assets/InterSemiBold.ttf)

# ---- Shared code -------------------------------------------------------------
set(FATBOARD_COMMON_SOURCES
  Source/Common/FatboardLookAndFeel.cpp
  Source/Common/FatboardLookAndFeel.h
  Source/Common/FatboardWidgets.h
  Source/Common/CleanupEngine.h
  Source/Common/VoiceCurve.h
  Source/Common/AirEngine.h)

set(FATBOARD_FORMATS VST3 Standalone)
if(APPLE)
  list(APPEND FATBOARD_FORMATS AU)
endif()

function(fatboard_configure target)
  target_compile_definitions(${target} PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)
  target_link_libraries(${target} PRIVATE
    FatboardAssets
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
  if(FATBOARD_LTO)
    target_link_libraries(${target} PUBLIC juce::juce_recommended_lto_flags)
  endif()
  target_include_directories(${target} PRIVATE Source/Common)
endfunction()

# ---- Scrub (free) ------------------------------------------------
juce_add_plugin(Scrub
  PRODUCT_NAME "Scrub"
  COMPANY_NAME "Fatboard"
  BUNDLE_ID "com.fatboard.scrub"
  PLUGIN_MANUFACTURER_CODE Fatb
  PLUGIN_CODE Fscr
  FORMATS ${FATBOARD_FORMATS}
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE
  VST3_CATEGORIES Fx EQ Dynamics
  AU_MAIN_TYPE kAudioUnitType_Effect)

target_sources(Scrub PRIVATE
  Source/Scrub/CleanupProcessor.cpp
  Source/Scrub/CleanupProcessor.h
  Source/Scrub/CleanupEditor.cpp
  Source/Scrub/CleanupEditor.h
  ${FATBOARD_COMMON_SOURCES})
fatboard_configure(Scrub)

# ---- Scrub Pro ------------------------------------------------------------
juce_add_plugin(ScrubPro
  PRODUCT_NAME "Scrub Pro"
  COMPANY_NAME "Fatboard"
  BUNDLE_ID "com.fatboard.scrubpro"
  PLUGIN_MANUFACTURER_CODE Fatb
  PLUGIN_CODE Fspr
  FORMATS ${FATBOARD_FORMATS}
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE
  VST3_CATEGORIES Fx EQ Dynamics
  AU_MAIN_TYPE kAudioUnitType_Effect)

target_sources(ScrubPro PRIVATE
  Source/ScrubPro/ProProcessor.cpp
  Source/ScrubPro/ProProcessor.h
  Source/ScrubPro/ProEditor.cpp
  Source/ScrubPro/ProEditor.h
  ${FATBOARD_COMMON_SOURCES})
fatboard_configure(ScrubPro)

# ---- Harun (templates for a recorded double / harmony: L/R layer, Scrub-matched EQ, hall + echo) ----
juce_add_plugin(Harun
  PRODUCT_NAME "Harun"
  COMPANY_NAME "Fatboard"
  BUNDLE_ID "com.fatboard.harun"
  PLUGIN_MANUFACTURER_CODE Fatb
  PLUGIN_CODE Fhar
  FORMATS ${FATBOARD_FORMATS}
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE
  VST3_CATEGORIES Fx Spatial
  AU_MAIN_TYPE kAudioUnitType_Effect)

target_sources(Harun PRIVATE
  Source/Harun/HarunProcessor.cpp
  Source/Harun/HarunProcessor.h
  Source/Harun/HarunEditor.cpp
  Source/Harun/HarunEditor.h
  Source/Harun/HarunEngine.h
  Source/Harun/HarunSpace.h
  ${FATBOARD_COMMON_SOURCES})
fatboard_configure(Harun)
target_include_directories(Harun PRIVATE Source/Harun)

# ---- Spit (beatbox → drum MIDI) -----------------------------------------------
juce_add_plugin(Spit
  PRODUCT_NAME "Spit"
  COMPANY_NAME "Fatboard"
  BUNDLE_ID "com.fatboard.spit"
  PLUGIN_MANUFACTURER_CODE Fatb
  PLUGIN_CODE Fspt
  FORMATS ${FATBOARD_FORMATS}
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT TRUE
  COPY_PLUGIN_AFTER_BUILD FALSE
  VST3_CATEGORIES Fx Tools
  AU_MAIN_TYPE kAudioUnitType_MusicEffect)

target_sources(Spit PRIVATE
  Source/Spit/SpitProcessor.cpp
  Source/Spit/SpitProcessor.h
  Source/Spit/SpitEditor.cpp
  Source/Spit/SpitEditor.h
  Source/Spit/SpitEngine.h
  ${FATBOARD_COMMON_SOURCES})
fatboard_configure(Spit)
target_include_directories(Spit PRIVATE Source/Spit)

# ---- Smash (drum bus) ----------------------------------------------------------
juce_add_plugin(Smash
  PRODUCT_NAME "Smash"
  COMPANY_NAME "Fatboard"
  BUNDLE_ID "com.fatboard.smash"
  PLUGIN_MANUFACTURER_CODE Fatb
  PLUGIN_CODE Fsms
  FORMATS ${FATBOARD_FORMATS}
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE
  VST3_CATEGORIES Fx Dynamics
  AU_MAIN_TYPE kAudioUnitType_Effect)
target_sources(Smash PRIVATE
  Source/Smash/SmashProcessor.cpp Source/Smash/SmashProcessor.h
  Source/Smash/SmashEditor.cpp Source/Smash/SmashEditor.h
  Source/Smash/SmashEngine.h
  ${FATBOARD_COMMON_SOURCES})
fatboard_configure(Smash)
target_include_directories(Smash PRIVATE Source/Smash)

# ---- Korrent (master: loudness, 808-safe clipping, playback check) -----------------
juce_add_plugin(Korrent
  PRODUCT_NAME "Korrent"
  COMPANY_NAME "Fatboard"
  BUNDLE_ID "com.fatboard.korrent"
  PLUGIN_MANUFACTURER_CODE Fatb
  PLUGIN_CODE Fkor
  FORMATS ${FATBOARD_FORMATS}
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE
  VST3_CATEGORIES Fx Mastering
  AU_MAIN_TYPE kAudioUnitType_Effect)
target_sources(Korrent PRIVATE
  Source/Korrent/KorrentProcessor.cpp Source/Korrent/KorrentProcessor.h
  Source/Korrent/KorrentEditor.cpp Source/Korrent/KorrentEditor.h
  Source/Korrent/KorrentEngine.h
  ${FATBOARD_COMMON_SOURCES})
fatboard_configure(Korrent)
target_include_directories(Korrent PRIVATE Source/Korrent)

# ---- Offline engine test (optional) -----------------------------------------
option(FATBOARD_BUILD_TESTS "Build the offline engine test" OFF)
if(FATBOARD_BUILD_TESTS)
  juce_add_console_app(FatboardEngineTest PRODUCT_NAME "FatboardEngineTest")
  target_sources(FatboardEngineTest PRIVATE Tests/EngineTest.cpp)
  target_include_directories(FatboardEngineTest PRIVATE Source/Common)
  target_compile_definitions(FatboardEngineTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
  target_link_libraries(FatboardEngineTest PRIVATE juce::juce_dsp PUBLIC juce::juce_recommended_config_flags)


  juce_add_console_app(HarunTest PRODUCT_NAME "HarunTest")
  target_sources(HarunTest PRIVATE Tests/HarunTest.cpp)
  target_include_directories(HarunTest PRIVATE Source/Common Source/Harun)
  target_compile_definitions(HarunTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
  target_link_libraries(HarunTest PRIVATE juce::juce_dsp PUBLIC juce::juce_recommended_config_flags)

  juce_add_console_app(SpitTest PRODUCT_NAME "SpitTest")
  target_sources(SpitTest PRIVATE Tests/SpitTest.cpp)
  target_include_directories(SpitTest PRIVATE Source/Common Source/Spit)
  target_compile_definitions(SpitTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
  target_link_libraries(SpitTest PRIVATE juce::juce_dsp juce::juce_audio_formats PUBLIC juce::juce_recommended_config_flags)

  juce_add_console_app(SmashTest PRODUCT_NAME "SmashTest")
  target_sources(SmashTest PRIVATE Tests/SmashTest.cpp)
  target_include_directories(SmashTest PRIVATE Source/Common Source/Smash)
  target_compile_definitions(SmashTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
  target_link_libraries(SmashTest PRIVATE juce::juce_dsp juce::juce_audio_formats PUBLIC juce::juce_recommended_config_flags)

  juce_add_console_app(KorrentTest PRODUCT_NAME "KorrentTest")
  target_sources(KorrentTest PRIVATE Tests/KorrentTest.cpp)
  target_include_directories(KorrentTest PRIVATE Source/Korrent)
  target_compile_definitions(KorrentTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
  target_link_libraries(KorrentTest PRIVATE juce::juce_dsp PUBLIC juce::juce_recommended_config_flags)

  juce_add_console_app(CurveTest PRODUCT_NAME "CurveTest")
  target_sources(CurveTest PRIVATE Tests/CurveTest.cpp)
  target_include_directories(CurveTest PRIVATE Source/Common)
  target_compile_definitions(CurveTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
  target_link_libraries(CurveTest PRIVATE juce::juce_dsp PUBLIC juce::juce_recommended_config_flags)

endif()
