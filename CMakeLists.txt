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

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/App.cpp
    src/App.hpp
    src/audio/ProgramLoudness.cpp
    src/audio/ProgramLoudness.hpp
    src/audio/SourceLevels.cpp
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
    src/ui/ViewerDisplay.cpp
    src/ui/ViewerDisplay.hpp
    src/ui/ViewerToolbar.cpp
    src/ui/ViewerToolbar.hpp
    src/ui/ViewerDock.cpp
    src/ui/ViewerDock.hpp
    src/ui/FullscreenWindow.cpp
    src/ui/FullscreenWindow.hpp
    src/ui/SettingsDialog.cpp
    src/ui/SettingsDialog.hpp
    src/ui/MultiviewPanel.cpp
    src/ui/MultiviewPanel.hpp
    src/ui/NetworkBar.cpp
    src/ui/NetworkBar.hpp
    src/ui/Paint.hpp
    src/ui/SidePanel.cpp
    src/ui/SidePanel.hpp
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

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
