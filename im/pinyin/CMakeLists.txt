set(PINYIN_SOURCES
    pinyin.cpp
    customphrase.cpp
    symboldictionary.cpp
    workerthread.cpp
    pinyincandidate.cpp
    pinyinenginefactory.cpp
)

add_fcitx5_addon(pinyin ${PINYIN_SOURCES})
target_link_libraries(pinyin Fcitx5::Core Fcitx5::Config LibIME::Pinyin Fcitx5::Module::Punctuation Fcitx5::Module::QuickPhrase Fcitx5::Module::Notifications Fcitx5::Module::Spell Fcitx5::Module::PinyinHelper Pthread::Pthread)

if (TARGET Fcitx5::Module::LuaAddonLoader)
    target_compile_definitions(pinyin PRIVATE -DFCITX_HAS_LUA)
    target_link_libraries(pinyin Fcitx5::Module::LuaAddonLoader)
    install(FILES pinyin.lua DESTINATION "${FCITX_INSTALL_PKGDATADIR}/lua/imeapi/extensions"
            COMPONENT config)
endif()

install(TARGETS pinyin DESTINATION "${CMAKE_INSTALL_LIBDIR}/fcitx5")
fcitx5_translate_desktop_file(pinyin.conf.in pinyin.conf)
fcitx5_translate_desktop_file(shuangpin.conf.in shuangpin.conf)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/pinyin.conf" "${CMAKE_CURRENT_BINARY_DIR}/shuangpin.conf" DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/inputmethod"
        COMPONENT config)
configure_file(pinyin-addon.conf.in.in pinyin-addon.conf.in)
fcitx5_translate_desktop_file("${CMAKE_CURRENT_BINARY_DIR}/pinyin-addon.conf.in" pinyin-addon.conf)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/pinyin-addon.conf" RENAME pinyin.conf DESTINATION "${FCITX_INSTALL_PKGDATADIR}/addon"
        COMPONENT config)

install(FILES "symbols" DESTINATION "${FCITX_INSTALL_PKGDATADIR}/pinyin" COMPONENT config)

if (ENABLE_DATA)

  add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/chaizi.dict"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/chaizi.txt" LibIME::pinyindict
    COMMAND LibIME::pinyindict "${CMAKE_CURRENT_SOURCE_DIR}/chaizi.txt" "${CMAKE_CURRENT_BINARY_DIR}/chaizi.dict")
  add_custom_target(chaizi-dict ALL DEPENDS "chaizi.dict")
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/chaizi.dict" DESTINATION "${FCITX_INSTALL_PKGDATADIR}/pinyin")
endif()
