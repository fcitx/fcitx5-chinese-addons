set(FULLWIDTH_SOURCES
    fullwidth.cpp
)
add_library(fullwidth MODULE ${FULLWIDTH_SOURCES})
target_link_libraries(fullwidth Fcitx5::Core Fcitx5::Config Fcitx5::Module::Notifications)
install(TARGETS fullwidth DESTINATION "${CMAKE_INSTALL_LIBDIR}/fcitx5")
configure_file(fullwidth.conf.in.in fullwidth.conf.in)
fcitx5_translate_desktop_file("${CMAKE_CURRENT_BINARY_DIR}/fullwidth.conf.in" fullwidth.conf)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/fullwidth.conf" DESTINATION "${FCITX_INSTALL_PKGDATADIR}/addon")
