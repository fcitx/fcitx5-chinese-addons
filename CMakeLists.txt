cmake_minimum_required(VERSION 3.6.0)
project(fcitx5-chinese-addons VERSION 5.0.17)

find_package(ECM REQUIRED 1.0.0)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})
include(FeatureSummary)
include(GNUInstallDirs)
include(ECMSetupVersion)
include(ECMUninstallTarget)

find_package(Fcitx5Core 5.0.11 REQUIRED)
find_package(Fcitx5Module REQUIRED COMPONENTS Notifications QuickPhrase Spell Clipboard TestFrontend TestIM)
find_package(Fcitx5ModuleLuaAddonLoader)
find_package(Pthread REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(Gettext REQUIRED)
find_package(fmt REQUIRED)

if (TARGET fmt::fmt-header-only)
  set(FMT_TARGET fmt::fmt-header-only)
else()
  set(FMT_TARGET fmt::fmt)
endif ()

include("${FCITX_INSTALL_CMAKECONFIG_DIR}/Fcitx5Utils/Fcitx5CompilerSettings.cmake")

#######################################################################
# Options
#######################################################################
option(ENABLE_TEST "Build Test" On)
option(ENABLE_COVERAGE "Build the project with gcov support (Need ENABLE_TEST=On)" Off)
set(GCOV_TOOL "gcov" CACHE STRING "Path to gcov tool used by coverage.")
option(ENABLE_OPENCC "Enable OpenCC for chttrans" On)
option(ENABLE_GUI "Enable Configure Gui" On)
option(ENABLE_BROWSER "Enable built-in browser." On)
option(USE_WEBKIT "Use WebKit as browser backend." On)
option(ENABLE_CLOUDPINYIN "Build cloud pinyin addon" On)

if (ENABLE_OPENCC)
    find_package(OpenCC REQUIRED)
endif()

if (ENABLE_GUI)
    find_package(Qt5 REQUIRED COMPONENTS DBus Concurrent)
    find_package(Fcitx5Qt5WidgetsAddons REQUIRED)
    find_package(Fcitx5Qt5DBusAddons REQUIRED)
    if (ENABLE_BROWSER)
        if (USE_WEBKIT)
            find_package(Qt5WebKitWidgets REQUIRED)
        else()
            find_package(Qt5WebEngineWidgets REQUIRED)
        endif()
    endif()
endif()

find_package(Boost 1.61 REQUIRED COMPONENTS iostreams)
find_package(LibIMEPinyin 1.0.14 REQUIRED)
find_package(LibIMETable 1.0.12 REQUIRED)

if (ENABLE_CLOUDPINYIN)
    pkg_check_modules(Curl REQUIRED IMPORTED_TARGET "libcurl")
endif()

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

add_definitions(-DFCITX_GETTEXT_DOMAIN=\"fcitx5-chinese-addons\")
add_definitions(-DQT_NO_KEYWORDS)
fcitx5_add_i18n_definition()

add_subdirectory(modules)
add_subdirectory(im)
add_subdirectory(po)
add_subdirectory(tools)

if (ENABLE_GUI)
    add_subdirectory(gui)
endif()

add_subdirectory(icon)

if (ENABLE_TEST)
    enable_testing()
    add_subdirectory(test)

    if (ENABLE_COVERAGE)
        add_custom_target(coverage
                        COMMAND "${CMAKE_CTEST_COMMAND}"
                        COMMAND lcov --gcov-tool "${GCOV_TOOL}" --no-external --capture --directory ./ -b "${CMAKE_CURRENT_SOURCE_DIR}" --output-file coverage.info
                        COMMAND genhtml coverage.info --output-directory "coverage_pages"
                        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    endif()
endif ()

fcitx5_translate_desktop_file(org.fcitx.Fcitx5.Addon.ChineseAddons.metainfo.xml.in
    org.fcitx.Fcitx5.Addon.ChineseAddons.metainfo.xml XML)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/org.fcitx.Fcitx5.Addon.ChineseAddons.metainfo.xml" DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
