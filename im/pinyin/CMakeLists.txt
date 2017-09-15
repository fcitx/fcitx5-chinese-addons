set(PINYIN_SOURCES
    pinyin.cpp
)
add_library(pinyin MODULE ${PINYIN_SOURCES})
target_link_libraries(pinyin Fcitx5::Core Fcitx5::Config LibIME::Pinyin Boost::iostreams Fcitx5::Module::Punctuation Fcitx5::Module::Fullwidth Fcitx5::Module::QuickPhrase Fcitx5::Module::CloudPinyin Fcitx5::Module::Notifications)
set_target_properties(pinyin PROPERTIES PREFIX "")
install(TARGETS pinyin DESTINATION "${CMAKE_INSTALL_LIBDIR}/fcitx5")
fcitx5_translate_desktop_file(pinyin.conf.in pinyin.conf)
fcitx5_translate_desktop_file(shuangpin.conf.in shuangpin.conf)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/pinyin.conf" "${CMAKE_CURRENT_BINARY_DIR}/shuangpin.conf" DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/inputmethod")
fcitx5_translate_desktop_file(pinyin-addon.conf.in pinyin-addon.conf)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/pinyin-addon.conf" RENAME pinyin.conf DESTINATION "${FCITX_INSTALL_PKGDATADIR}/addon")

