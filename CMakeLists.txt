cmake_minimum_required(VERSION 3.4.0)
project(fcitx5-chinese-addons VERSION 0.0.1)

find_package(ECM REQUIRED 1.0.0)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})
include(FeatureSummary)
include(GNUInstallDirs)
include(ECMSetupVersion)
include(ECMUninstallTarget)

find_package(Fcitx5Core REQUIRED)
find_package(Fcitx5Module REQUIRED COMPONENTS Notifications QuickPhrase)
find_package(LibIntl REQUIRED)
find_package(Pthread REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(Gettext REQUIRED)
pkg_check_modules(Curl REQUIRED IMPORTED_TARGET "libcurl")

include("${FCITX_INSTALL_CMAKECONFIG_DIR}/Fcitx5Utils/Fcitx5CompilerSettings.cmake")

#######################################################################
# Options
#######################################################################
option(ENABLE_TEST "Build Test" On)
option(ENABLE_OPENCC "Enable OpenCC for chttrans" On)

if (ENABLE_OPENCC)
    find_package(OpenCC REQUIRED)
endif()

find_package(Boost 1.61 REQUIRED COMPONENTS iostreams)
find_package(LibIMEPinyin REQUIRED)
find_package(LibIMETable REQUIRED)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

add_subdirectory(modules)
add_subdirectory(im)

if (ENABLE_TEST)
    enable_testing()
    add_subdirectory(test)

    if (ENABLE_COVERAGE)
        add_custom_target(coverage
                        COMMAND "${CMAKE_CTEST_COMMAND}"
                        COMMAND lcov --capture --directory ./ --output-file coverage.info
                        COMMAND genhtml coverage.info --output-directory "coverage_pages"
                        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    endif()
endif ()

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
