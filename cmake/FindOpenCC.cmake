find_package(PkgConfig)

pkg_check_modules(PKG_OPENCC QUIET opencc)

set(OPENCC_DEFINITIONS ${PKG_OPENCC_CFLAGS_OTHER})
set(OPENCC_VERSION ${PKG_OPENCC_VERSION})

find_path(OPENCC_INCLUDE_DIR
    NAMES opencc.h
    HINTS ${PKG_OPENCC_INCLUDE_DIRS}
)
find_library(OPENCC_LIBRARY
    NAMES opencc
    HINTS ${PKG_OPENCC_LIBRARY_DIRS}
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(OpenCC
    FOUND_VAR
        OPENCC_FOUND
    REQUIRED_VARS
        OPENCC_LIBRARY
        OPENCC_INCLUDE_DIR
    VERSION_VAR
        OPENCC_VERSION
)

if(OPENCC_FOUND AND NOT TARGET OpenCC::OpenCC)
    add_library(OpenCC::OpenCC UNKNOWN IMPORTED)
    set_target_properties(OpenCC::OpenCC PROPERTIES
        IMPORTED_LOCATION "${OPENCC_LIBRARY}"
        INTERFACE_COMPILE_OPTIONS "${OPENCC_DEFINITIONS}"
        INTERFACE_INCLUDE_DIRECTORIES "${OPENCC_INCLUDE_DIR}"
    )
endif()

mark_as_advanced(OPENCC_INCLUDE_DIR OPENCC_LIBRARY)

include(FeatureSummary)
set_package_properties(OPENCC PROPERTIES
    URL "https://github.com/BYVoid/OpenCC/"
    DESCRIPTION "Library for Open Chinese Convert"
)

