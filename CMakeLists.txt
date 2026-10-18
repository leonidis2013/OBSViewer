cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
target_compile_features(${CMAKE_PROJECT_NAME} PRIVATE cxx_std_17)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

find_package(obs-frontend-api REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)

find_package(Qt6 REQUIRED COMPONENTS Widgets Core Gui)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets Qt6::Gui)
target_compile_options(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header>
    $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-comma>
    $<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang>:-Wno-shorten-64-to-32>
)
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON)

# The updater talks to GitHub with the system libcurl and stores its token in the Keychain.
find_package(CURL REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE CURL::libcurl)

if(APPLE)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE "$<LINK_LIBRARY:FRAMEWORK,Security.framework>"
                                                      "$<LINK_LIBRARY:FRAMEWORK,CoreFoundation.framework>")
endif()

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

# ---------------------------------------------------------------------------
# Master bus plugin host (JUCE). Built as its own static library so JUCE never
# mixes with the Qt / OBS code, and with Objective-C ARC off (JUCE requires that;
# the OBS template turns ARC on for everything else).
include(FetchContent)
FetchContent_Declare(
  JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.15
  GIT_SHALLOW ON
)
FetchContent_MakeAvailable(JUCE)

add_library(obsviewer_bus STATIC src/bus/Engine.cpp src/bus/Engine.hpp)
target_compile_features(obsviewer_bus PRIVATE cxx_std_17)
target_include_directories(obsviewer_bus PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(
  obsviewer_bus
  PRIVATE juce::juce_audio_processors juce::juce_gui_basics juce::juce_recommended_config_flags
)
target_compile_definitions(
  obsviewer_bus
  PRIVATE
    JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
    JUCE_STANDALONE_APPLICATION=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    JUCE_PLUGINHOST_LADSPA=0
    JUCE_PLUGINHOST_LV2=0
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_MODAL_LOOPS_PERMITTED=0
    JUCE_DISABLE_JUCE_VERSION_PRINTING=1
    DONT_SET_USING_JUCE_NAMESPACE=1
)
set_target_properties(
  obsviewer_bus
  PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC NO
    XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_WEAK NO
    XCODE_ATTRIBUTE_CLANG_WARN_BLOCK_CAPTURE_AUTORELEASING NO
    XCODE_ATTRIBUTE_GCC_TREAT_WARNINGS_AS_ERRORS NO
    XCODE_ATTRIBUTE_CLANG_WARN_COMMA NO
    XCODE_ATTRIBUTE_GCC_WARN_64_TO_32_BIT_CONVERSION NO
    XCODE_ATTRIBUTE_GCC_WARN_SIGN_COMPARE NO
)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE obsviewer_bus)
if(APPLE)
  # Frameworks the JUCE plugin host needs (AU editors live in CoreAudioKit).
  target_link_libraries(
    ${CMAKE_PROJECT_NAME}
    PRIVATE
      "$<LINK_LIBRARY:FRAMEWORK,CoreAudioKit.framework>"
      "$<LINK_LIBRARY:FRAMEWORK,AudioToolbox.framework>"
      "$<LINK_LIBRARY:FRAMEWORK,AudioUnit.framework>"
      "$<LINK_LIBRARY:FRAMEWORK,CoreAudio.framework>"
      "$<LINK_LIBRARY:FRAMEWORK,CoreMIDI.framework>"
      "$<LINK_LIBRARY:FRAMEWORK,Accelerate.framework>"
      "$<LINK_LIBRARY:FRAMEWORK,Cocoa.framework>"
      "$<LINK_LIBRARY:FRAMEWORK,QuartzCore.framework>"
      "$<LINK_LIBRARY:FRAMEWORK,IOKit.framework>"
  )
endif()

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/App.cpp
    src/App.hpp
    src/Hotkeys.cpp
    src/Hotkeys.hpp
    src/audio/ProgramLoudness.cpp
    src/audio/ProgramLoudness.hpp
    src/audio/SourceLevels.cpp
    src/bus/MasterBus.cpp
    src/bus/MasterBus.hpp
    src/audio/SourceLevels.hpp
    src/core/LevelMath.hpp
    src/audio/meters/Biquad.h
    src/audio/meters/LoudnessMeter.h
    src/audio/meters/LoudnessPresets.h
    src/audio/meters/TpFilter.h
    src/audio/meters/TruePeakMeter.h
    src/core/Health.cpp
    src/core/Health.hpp
    src/core/History.hpp
    src/metrics/Metrics.cpp
    src/metrics/Metrics.hpp
    src/metrics/PictureMonitor.cpp
    src/metrics/PictureMonitor.hpp
    src/core/Settings.cpp
    src/core/Settings.hpp
    src/core/Version.cpp
    src/core/Version.hpp
    src/viewer/Layouts.cpp
    src/viewer/Layouts.hpp
    src/viewer/ViewerState.cpp
    src/viewer/ViewerState.hpp
    src/viewer/Renderer.cpp
    src/viewer/Renderer.hpp
    src/ui/Theme.cpp
    src/ui/Theme.hpp
    src/ui/ControlDock.cpp
    src/ui/ControlDock.hpp
    src/ui/ViewerDisplay.cpp
    src/ui/ViewerDisplay.hpp
    src/ui/ViewerDock.cpp
    src/ui/ViewerDock.hpp
    src/ui/ViewerWindow.cpp
    src/ui/ViewerWindow.hpp
    src/ui/SettingsDialog.cpp
    src/ui/SettingsDialog.hpp
    src/ui/MultiviewPanel.cpp
    src/ui/MultiviewPanel.hpp
    src/ui/Paint.hpp
    src/ui/SidePanel.cpp
    src/ui/SidePanel.hpp
    src/ui/MasterBusWindow.cpp
    src/ui/MasterBusWindow.hpp
    src/ui/Menu.cpp
    src/ui/Menu.hpp
    src/update/Updater.cpp
    src/update/Updater.hpp
    src/update/ReleaseInfo.cpp
    src/update/ReleaseInfo.hpp
    src/update/UpdateDialog.cpp
    src/update/UpdateDialog.hpp
    src/update/Keychain.cpp
    src/update/Keychain.hpp
)

# Plugin scanner helper. Scanning loads every plugin on the system, and some crash
# while being inspected, so each one is scanned in this separate process. It ships
# inside the plugin bundle at Contents/Helpers/obsviewer-scan.
add_executable(obsviewer-scan src/bus/ScanMain.cpp)
target_compile_features(obsviewer-scan PRIVATE cxx_std_17)
target_link_libraries(obsviewer-scan PRIVATE obsviewer_bus)
if(APPLE)
  target_link_libraries(
    obsviewer-scan
    PRIVATE
      "$<LINK_LIBRARY:FRAMEWORK,CoreAudioKit.framework>"
      "$<LINK_LIBRARY:FRAMEWORK,AudioToolbox.framework>"
      "$<LINK_LIBRARY:FRAMEWORK,AudioUnit.framework>"
      "$<LINK_LIBRARY:FRAMEWORK,CoreAudio.framework>"
      "$<LINK_LIBRARY:FRAMEWORK,CoreMIDI.framework>"
      "$<LINK_LIBRARY:FRAMEWORK,Accelerate.framework>"
      "$<LINK_LIBRARY:FRAMEWORK,Cocoa.framework>"
      "$<LINK_LIBRARY:FRAMEWORK,QuartzCore.framework>"
      "$<LINK_LIBRARY:FRAMEWORK,IOKit.framework>"
      "$<LINK_LIBRARY:FRAMEWORK,Security.framework>"
  )
  set_target_properties(
    obsviewer-scan
    PROPERTIES
      XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/macos/scan-entitlements.plist"
      XCODE_ATTRIBUTE_SKIP_INSTALL YES
      XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC NO
  )
endif()
add_dependencies(${CMAKE_PROJECT_NAME} obsviewer-scan)
# Copied in before Xcode signs the plugin bundle, so the bundle's signature covers it.
add_custom_command(
  TARGET ${CMAKE_PROJECT_NAME}
  POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:${CMAKE_PROJECT_NAME}>/Helpers"
  COMMAND
    "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:obsviewer-scan>"
    "$<TARGET_BUNDLE_CONTENT_DIR:${CMAKE_PROJECT_NAME}>/Helpers/obsviewer-scan"
  COMMENT "Embed obsviewer-scan in the plugin bundle"
  VERBATIM
)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
