cmake_minimum_required(VERSION 3.22)
project(Fatboard VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

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
  Source/Common/VocalModes.h)

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
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
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

# ---- Harun (harmony / doubler) -----------------------------------------------
# Pitch shifting: Signalsmith Stretch + Signalsmith Linear (both MIT, vendored in ThirdParty/)
add_library(signalsmith INTERFACE)
target_include_directories(signalsmith INTERFACE
  ThirdParty/signalsmith-stretch/include
  ThirdParty/signalsmith-linear/include)
if(MSVC)
  target_compile_options(signalsmith INTERFACE /bigobj)
endif()

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
  VST3_CATEGORIES Fx Pitch\ Shift
  AU_MAIN_TYPE kAudioUnitType_Effect)

target_sources(Harun PRIVATE
  Source/Harun/HarunProcessor.cpp
  Source/Harun/HarunProcessor.h
  Source/Harun/HarunEditor.cpp
  Source/Harun/HarunEditor.h
  Source/Harun/PitchTracker.h
  Source/Harun/HarmonyLogic.h
  Source/Harun/HarunEngine.h
  ${FATBOARD_COMMON_SOURCES})
fatboard_configure(Harun)
target_link_libraries(Harun PRIVATE signalsmith)
target_include_directories(Harun PRIVATE Source/Harun)

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
  target_link_libraries(HarunTest PRIVATE juce::juce_dsp juce::juce_audio_formats signalsmith PUBLIC juce::juce_recommended_config_flags)
endif()
