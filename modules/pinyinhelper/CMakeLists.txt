add_subdirectory(data)

set(PINYINHELPER_SOURCES
    pinyinhelper.cpp
    pinyinlookup.cpp
    stroke.cpp
)
add_library(pinyinhelper MODULE ${PINYINHELPER_SOURCES})
target_link_libraries(pinyinhelper Fcitx5::Core Fcitx5::Config LibIME::Core Boost::iostreams)
set_target_properties(pinyinhelper PROPERTIES PREFIX "")
install(TARGETS pinyinhelper DESTINATION "${CMAKE_INSTALL_LIBDIR}/fcitx5")
fcitx5_translate_desktop_file(pinyinhelper.conf.in pinyinhelper.conf)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/pinyinhelper.conf" DESTINATION "${FCITX_INSTALL_PKGDATADIR}/addon")

fcitx5_export_module(PinyinHelper TARGET pinyinhelper BUILD_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}" HEADERS pinyinhelper_public.h INSTALL)

