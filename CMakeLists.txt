cmake_minimum_required(VERSION 3.22)
project(Fatboard VERSION 0.14.0 LANGUAGES C CXX)

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

# ---- Licensing (plugin <-> fat-board.com account) ------------------------------
# OFF: every plugin is open (development builds, and until the shop is live).
# ON:  paid plugins run in demo mode until the user logs in. Needs the public key
#      from `npm run keys` in fatboard-web:
#      cmake -DFATBOARD_LICENSING=ON -DFATBOARD_LICENSE_PUBKEY=<64 hex chars> [-DFATBOARD_API_URL=https://fat-board.com]
option(FATBOARD_LICENSING "Paid plugins need a fat-board.com login" OFF)
set(FATBOARD_LICENSE_PUBKEY "" CACHE STRING "Ed25519 public key (hex) the license tokens are checked against")
set(FATBOARD_API_URL "https://fat-board.com" CACHE STRING "Where the plugins log in")
if(FATBOARD_LICENSING AND NOT FATBOARD_LICENSE_PUBKEY MATCHES "^[0-9a-fA-F]+$")
  message(FATAL_ERROR "FATBOARD_LICENSING=ON needs -DFATBOARD_LICENSE_PUBKEY=<64 hex chars>")
endif()

# Ed25519 signature check (vendored, zlib license, see ThirdParty/ed25519)
add_library(fatboard_ed25519 STATIC
  ThirdParty/ed25519/verify.c ThirdParty/ed25519/ge.c ThirdParty/ed25519/fe.c
  ThirdParty/ed25519/sc.c ThirdParty/ed25519/sha512.c)
target_include_directories(fatboard_ed25519 PUBLIC ThirdParty/ed25519)
set_target_properties(fatboard_ed25519 PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---- Shared code -------------------------------------------------------------
set(FATBOARD_COMMON_SOURCES
  Source/Common/FatboardLookAndFeel.cpp
  Source/Common/FatboardLookAndFeel.h
  Source/Common/FatboardWidgets.h
  Source/Common/CleanupEngine.h
  Source/Common/VoiceCurve.h
  Source/Common/AirEngine.h
  Source/Common/License/LicenseClient.cpp
  Source/Common/License/LicenseClient.h
  Source/Common/License/LicenseBar.h)

set(FATBOARD_FORMATS VST3 Standalone)
if(APPLE)
  list(APPEND FATBOARD_FORMATS AU)
endif()

function(fatboard_configure target)
  target_compile_definitions(${target} PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    FATBOARD_LICENSING=$<BOOL:${FATBOARD_LICENSING}>
    FATBOARD_LICENSE_PUBKEY="${FATBOARD_LICENSE_PUBKEY}"
    FATBOARD_API_URL="${FATBOARD_API_URL}")
  target_include_directories(${target} PRIVATE Source/Common/License)
  target_link_libraries(${target} PRIVATE
    fatboard_ed25519
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

# ---- Sog (reverse reverb: capture, render, drag the swell into the playlist) ------
juce_add_plugin(Sog
  PRODUCT_NAME "Sog"
  COMPANY_NAME "Fatboard"
  BUNDLE_ID "com.fatboard.sog"
  PLUGIN_MANUFACTURER_CODE Fatb
  PLUGIN_CODE Fsog
  FORMATS ${FATBOARD_FORMATS}
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE
  VST3_CATEGORIES Fx Reverb
  AU_MAIN_TYPE kAudioUnitType_Effect)
target_sources(Sog PRIVATE
  Source/Sog/SogProcessor.cpp Source/Sog/SogProcessor.h
  Source/Sog/SogEditor.cpp Source/Sog/SogEditor.h
  Source/Sog/SogRenderer.h
  ${FATBOARD_COMMON_SOURCES})
fatboard_configure(Sog)
target_include_directories(Sog PRIVATE Source/Sog)

# ---- Samt (optical vocal leveller, stereo) -----------------------------------------
juce_add_plugin(Samt
  PRODUCT_NAME "Samt"
  COMPANY_NAME "Fatboard"
  BUNDLE_ID "com.fatboard.samt"
  PLUGIN_MANUFACTURER_CODE Fatb
  PLUGIN_CODE Fsmt
  FORMATS ${FATBOARD_FORMATS}
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE
  VST3_CATEGORIES Fx Dynamics
  AU_MAIN_TYPE kAudioUnitType_Effect)
target_sources(Samt PRIVATE
  Source/Samt/SamtProcessor.cpp Source/Samt/SamtProcessor.h
  Source/Samt/SamtEditor.cpp Source/Samt/SamtEditor.h
  Source/Samt/SamtEngine.h
  ${FATBOARD_COMMON_SOURCES})
fatboard_configure(Samt)
target_include_directories(Samt PRIVATE Source/Samt)

# ---- Funke (ideas as MIDI: chords, melody, bass per vibe; can follow a drum loop) ---
juce_add_plugin(Funke
  PRODUCT_NAME "Funke"
  COMPANY_NAME "Fatboard"
  BUNDLE_ID "com.fatboard.funke"
  PLUGIN_MANUFACTURER_CODE Fatb
  PLUGIN_CODE Ffnk
  FORMATS ${FATBOARD_FORMATS}
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  COPY_PLUGIN_AFTER_BUILD FALSE
  VST3_CATEGORIES Instrument
  AU_MAIN_TYPE kAudioUnitType_MusicDevice)
target_sources(Funke PRIVATE
  Source/Funke/FunkeProcessor.cpp Source/Funke/FunkeProcessor.h
  Source/Funke/FunkeEditor.cpp Source/Funke/FunkeEditor.h
  Source/Funke/FunkeGen.h
  ${FATBOARD_COMMON_SOURCES})
fatboard_configure(Funke)
target_compile_definitions(Funke PUBLIC JUCE_USE_MP3AUDIOFORMAT=1)
target_include_directories(Funke PRIVATE Source/Funke)

# ---- Dig (PRIVATE tool, not a product: sample an old song – tempo, key, loop, chops) ----
juce_add_plugin(Dig
  PRODUCT_NAME "Dig"
  COMPANY_NAME "Fatboard"
  BUNDLE_ID "com.fatboard.dig"
  PLUGIN_MANUFACTURER_CODE Fatb
  PLUGIN_CODE Fdig
  FORMATS ${FATBOARD_FORMATS}
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  COPY_PLUGIN_AFTER_BUILD FALSE
  VST3_CATEGORIES Instrument Sampler
  AU_MAIN_TYPE kAudioUnitType_MusicDevice)
target_sources(Dig PRIVATE
  Source/Dig/DigProcessor.cpp Source/Dig/DigProcessor.h
  Source/Dig/DigEditor.cpp Source/Dig/DigEditor.h
  Source/Dig/DigAnalysis.h Source/Dig/DigRender.h
  ${FATBOARD_COMMON_SOURCES})
fatboard_configure(Dig)
target_compile_definitions(Dig PUBLIC JUCE_USE_MP3AUDIOFORMAT=1)
target_include_directories(Dig PRIVATE Source/Dig)

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

  juce_add_console_app(SmashTest PRODUCT_NAME "SmashTest")
  target_sources(SmashTest PRIVATE Tests/SmashTest.cpp)
  target_include_directories(SmashTest PRIVATE Source/Common Source/Smash)
  target_compile_definitions(SmashTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
  target_link_libraries(SmashTest PRIVATE juce::juce_dsp juce::juce_audio_formats PUBLIC juce::juce_recommended_config_flags)

  juce_add_console_app(LicenseTest PRODUCT_NAME "LicenseTest")
  target_sources(LicenseTest PRIVATE Tests/LicenseTest.cpp Source/Common/License/LicenseClient.cpp)
  target_include_directories(LicenseTest PRIVATE Source/Common/License)
  # Linux only: JUCE's built-in socket HTTP garbles a JSON POST body, so the test uses libcurl there.
  # Windows (WinINet) and macOS (NSURLSession) use the system's HTTP stack.
  if(UNIX AND NOT APPLE)
    find_package(CURL QUIET)
  endif()
  if(CURL_FOUND)
    target_compile_definitions(LicenseTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=1)
    target_link_libraries(LicenseTest PRIVATE CURL::libcurl)
  else()
    target_compile_definitions(LicenseTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
  endif()
  target_link_libraries(LicenseTest PRIVATE fatboard_ed25519 juce::juce_core juce::juce_events PUBLIC juce::juce_recommended_config_flags)

  juce_add_console_app(FunkeTest PRODUCT_NAME "FunkeTest")
  target_sources(FunkeTest PRIVATE Tests/FunkeTest.cpp)
  target_include_directories(FunkeTest PRIVATE Source/Funke)
  target_compile_definitions(FunkeTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
  target_link_libraries(FunkeTest PRIVATE juce::juce_audio_basics PUBLIC juce::juce_recommended_config_flags)

  juce_add_console_app(SamtTest PRODUCT_NAME "SamtTest")
  target_sources(SamtTest PRIVATE Tests/SamtTest.cpp)
  target_include_directories(SamtTest PRIVATE Source/Samt)
  target_compile_definitions(SamtTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
  target_link_libraries(SamtTest PRIVATE juce::juce_dsp PUBLIC juce::juce_recommended_config_flags)

  juce_add_console_app(DigTest PRODUCT_NAME "DigTest")
  target_sources(DigTest PRIVATE Tests/DigTest.cpp)
  target_include_directories(DigTest PRIVATE Source/Dig)
  target_compile_definitions(DigTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
  target_link_libraries(DigTest PRIVATE juce::juce_dsp PUBLIC juce::juce_recommended_config_flags)

  juce_add_console_app(SogTest PRODUCT_NAME "SogTest")
  target_sources(SogTest PRIVATE Tests/SogTest.cpp)
  target_include_directories(SogTest PRIVATE Source/Sog)
  target_compile_definitions(SogTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
  target_link_libraries(SogTest PRIVATE juce::juce_dsp PUBLIC juce::juce_recommended_config_flags)

  juce_add_console_app(CurveTest PRODUCT_NAME "CurveTest")
  target_sources(CurveTest PRIVATE Tests/CurveTest.cpp)
  target_include_directories(CurveTest PRIVATE Source/Common)
  target_compile_definitions(CurveTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
  target_link_libraries(CurveTest PRIVATE juce::juce_dsp PUBLIC juce::juce_recommended_config_flags)

endif()
