set(AMOUNTINWORD_SOURCES
amountinword.cpp
)
add_library(amountinword MODULE ${AMOUNTINWORD_SOURCES})
target_link_libraries(amountinword  Fcitx5::Core Fcitx5::Config LibIME::Core Boost::iostreams Fcitx5::Module::QuickPhrase Fcitx5::Module::Clipboard ${FMT_TARGET})
set_target_properties(amountinword PROPERTIES PREFIX "")
install(TARGETS amountinword DESTINATION "${CMAKE_INSTALL_LIBDIR}/fcitx5")
configure_file(amountinword.conf.in.in amountinword.conf.in)
fcitx5_translate_desktop_file("${CMAKE_CURRENT_BINARY_DIR}/amountinword.conf.in" amountinword.conf)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/amountinword.conf" DESTINATION "${FCITX_INSTALL_PKGDATADIR}/addon")