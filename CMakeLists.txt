cmake_minimum_required(VERSION 3.6.0)
project(fcitx5-chinese-addons VERSION 5.1.11)

set(REQUIRED_FCITX_VERSION 5.1.13)
set(REQUIRED_LIBIME_VERSION 1.1.13)

find_package(ECM REQUIRED 1.0.0)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})
include(FeatureSummary)
include(GNUInstallDirs)
include(ECMSetupVersion)
include(ECMUninstallTarget)

find_package(Fcitx5Core ${REQUIRED_FCITX_VERSION} REQUIRED)
find_package(Fcitx5Module REQUIRED COMPONENTS Notifications QuickPhrase Spell Clipboard)
find_package(Fcitx5ModuleLuaAddonLoader)
find_package(Pthread REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(Gettext REQUIRED)

include("${FCITX_INSTALL_CMAKECONFIG_DIR}/Fcitx5Utils/Fcitx5CompilerSettings.cmake")

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

#######################################################################
# Options
#######################################################################
option(ENABLE_TEST "Build Test" On)
option(ENABLE_COVERAGE "Build the project with gcov support (Need ENABLE_TEST=On)" Off)
set(GCOV_TOOL "gcov" CACHE STRING "Path to gcov tool used by coverage.")
option(ENABLE_OPENCC "Enable OpenCC for chttrans" On)
option(ENABLE_GUI "Enable Configure Gui" On)
option(ENABLE_BROWSER "Enable built-in browser." On)
option(ENABLE_CLOUDPINYIN "Build cloud pinyin addon" On)
option(ENABLE_DATA "Build data" On)

if (ENABLE_OPENCC)
    find_package(OpenCC 1.0.1 REQUIRED)
endif()

if (ENABLE_GUI)
    set(QT_MAJOR_VERSION 6)
    find_package(Qt${QT_MAJOR_VERSION} REQUIRED COMPONENTS Concurrent)
    find_package(Fcitx5Qt${QT_MAJOR_VERSION}WidgetsAddons REQUIRED)
    if (ENABLE_BROWSER)
        find_package(Qt${QT_MAJOR_VERSION}WebEngineWidgets REQUIRED)
    endif()
endif()

# We do not need json if OpenCC is not needed, but just try to find it anyway to save some ifelse logic
find_package(Boost 1.61 OPTIONAL_COMPONENTS json)
find_package(LibIMEPinyin ${REQUIRED_LIBIME_VERSION} REQUIRED)
find_package(LibIMETable ${REQUIRED_LIBIME_VERSION} REQUIRED)

if (ENABLE_CLOUDPINYIN)
    pkg_check_modules(Curl REQUIRED IMPORTED_TARGET "libcurl")
endif()

if (TARGET Boost::json)
    set(HAS_BOOST_JSON TRUE)
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
    find_package(Fcitx5ModuleTestFrontend REQUIRED)
    find_package(Fcitx5ModuleTestIM REQUIRED)
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
