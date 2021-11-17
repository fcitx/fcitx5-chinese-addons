set(PUNCTUATION_SOURCES
    punctuation.cpp
)

set(PUNCTUATION_DATA_FILES
  punc.mb.zh_CN
  punc.mb.zh_HK
  punc.mb.zh_TW
)

add_library(punctuation MODULE ${PUNCTUATION_SOURCES})
target_link_libraries(punctuation Fcitx5::Core Fcitx5::Config Boost::iostreams Fcitx5::Module::Notifications)
install(TARGETS punctuation DESTINATION "${CMAKE_INSTALL_LIBDIR}/fcitx5")
configure_file(punctuation.conf.in.in punctuation.conf.in)
fcitx5_translate_desktop_file("${CMAKE_CURRENT_BINARY_DIR}/punctuation.conf.in" punctuation.conf)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/punctuation.conf" DESTINATION "${FCITX_INSTALL_PKGDATADIR}/addon")
install(FILES ${PUNCTUATION_DATA_FILES} DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/punctuation")

fcitx5_export_module(Punctuation TARGET punctuation BUILD_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}" HEADERS punctuation_public.h INSTALL)
