cmake_minimum_required(VERSION 3.4.0)
project(fcitx5-chinese-addons)

find_package(ECM REQUIRED 1.0.0)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})
include(FeatureSummary)
include(GNUInstallDirs)
include(ECMSetupVersion)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_CXX_STANDARD 14)

find_package(Fcitx5Core REQUIRED)
find_package(Fcitx5Module REQUIRED COMPONENTS Notifications)
find_package(LibIntl REQUIRED)

option(ENABLE_OPENCC "Enable OpenCC for chttrans" On)

if (ENABLE_OPENCC)
    find_package(OpenCC REQUIRED)
endif()

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

add_subdirectory(modules)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
