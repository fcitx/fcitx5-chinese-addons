set(PINYIN_SOURCES
    pinyin.cpp
)
add_library(pinyin MODULE ${PINYIN_SOURCES})
target_link_libraries(pinyin Fcitx5::Core Fcitx5::Config LibIME::LibIME Boost::iostreams)
set_target_properties(pinyin PROPERTIES PREFIX "")
install(TARGETS pinyin DESTINATION "${CMAKE_INSTALL_LIBDIR}/fcitx5")
install(FILES pinyin.conf DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/addon")

