set(FULLWIDTH_SOURCES
    fullwidth.cpp
)
add_library(fullwidth MODULE ${FULLWIDTH_SOURCES})
target_link_libraries(fullwidth Fcitx5::Core Fcitx5::Config Fcitx5::Module::Notifications)
set_target_properties(fullwidth PROPERTIES PREFIX "")
install(TARGETS fullwidth DESTINATION "${CMAKE_INSTALL_LIBDIR}/fcitx5")
install(FILES fullwidth.conf DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/addon")

fcitx5_export_module(Fullwidth TARGET fullwidth BUILD_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}" HEADERS fullwidth_public.h INSTALL)
