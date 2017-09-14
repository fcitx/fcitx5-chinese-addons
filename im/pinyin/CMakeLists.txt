set(PINYIN_SOURCES
    pinyin.cpp
)
add_library(pinyin MODULE ${PINYIN_SOURCES})
target_link_libraries(pinyin Fcitx5::Core Fcitx5::Config LibIME::Pinyin Boost::iostreams Fcitx5::Module::Punctuation Fcitx5::Module::Fullwidth Fcitx5::Module::QuickPhrase Fcitx5::Module::CloudPinyin Fcitx5::Module::Notifications)
set_target_properties(pinyin PROPERTIES PREFIX "")
install(TARGETS pinyin DESTINATION "${CMAKE_INSTALL_LIBDIR}/fcitx5")
install(FILES pinyin.conf shuangpin.conf DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/inputmethod")
install(FILES pinyin-addon.conf RENAME pinyin.conf DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/addon")

